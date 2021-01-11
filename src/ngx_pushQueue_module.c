#include <ngx_config.h>
#include <ngx_core.h>
#include <ngx_http.h>
#include <amqp_tcp_socket.h>
#include <amqp.h>
#include <amqp_framing.h>
#include <syslog.h>
 
static ngx_int_t ngx_pushQueue_handler(ngx_http_request_t *r);
 
typedef struct {
  ngx_str_t mqType;
  ngx_http_complex_value_t *host;
  int isRabbitConnected;
  ngx_str_t username;
  ngx_str_t password;  
  ngx_str_t key;
  ngx_str_t rabbitHost;
  ngx_str_t rabbitExchange;
  ngx_str_t successTem;
  ngx_str_t failTem;
  amqp_socket_t* rabbitSocket;
  amqp_connection_state_t rabbitConn;
  ngx_http_upstream_conf_t   upstream;
} ngx_pushQueue_loc_conf_t;

typedef struct ngx_pushQueue_ctx_s  ngx_pushQueue_ctx_t;

typedef ngx_int_t (*ngx_pushQueue_filter_handler_ptr)
    (ngx_pushQueue_ctx_t *ctx, ssize_t bytes);

struct ngx_pushQueue_ctx_s {
    ngx_int_t                  query_count;
    ngx_http_request_t        *request;
    int                        state;
    size_t                     chunk_size;
    size_t                     chunk_bytes_read;
    size_t                     chunks_read;
    size_t                     chunk_count;
    ngx_pushQueue_filter_handler_ptr  filter;
};


static ngx_int_t ngx_pushQueue_createRequest(ngx_http_request_t *r);
static ngx_int_t ngx_pushQueue_processHeader(ngx_http_request_t *r);
static void ngx_pushQueue_finalizeRequest(ngx_http_request_t *r,ngx_int_t rc);
static ngx_int_t showSuccessMessage(ngx_http_request_t *r,char *message);
static ngx_int_t showErrorMessage(ngx_http_request_t *r,int type);
static char *ngx_pushQueue_addHost(ngx_conf_t *cf, ngx_command_t *cmd,void *conf);
static void getRequestMethod(ngx_http_request_t *r,char **method);
static char *ngx_pushQueue_getAuth(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *getMessageContent(ngx_http_request_t *r);
static void ngx_pushQueue_exitProcess(ngx_cycle_t* cycle);

static char *nginx_setPushKey(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){
    ngx_conf_set_str_slot(cf, cmd, conf);
    ngx_http_core_loc_conf_t *clcf;
    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    clcf->handler = ngx_pushQueue_handler;
    return NGX_CONF_OK;
} 


static ngx_command_t ngx_pushQueue_commands[] = {
  { 
    ngx_string("CQuick_mqType"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_pushQueue_loc_conf_t, mqType),
    NULL 
  },
  {
    ngx_string("CQuick_rabbitHost"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_pushQueue_loc_conf_t, rabbitHost),
    NULL
  },
  {
    ngx_string("CQuick_rabbitExchange"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_pushQueue_loc_conf_t, rabbitExchange),
    NULL
  },
  {
    ngx_string("CQuick_auth"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE12,
    ngx_pushQueue_getAuth,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
  },
  {
    ngx_string("CQuick_pass"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    ngx_pushQueue_addHost,
    NGX_HTTP_LOC_CONF_OFFSET,
    0,
    NULL
  },
  {
    ngx_string("CQuick_successTemplate"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_pushQueue_loc_conf_t, successTem),
    NULL
  },
  {
    ngx_string("CQuick_failTemplate"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    ngx_conf_set_str_slot,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_pushQueue_loc_conf_t, failTem),
    NULL
  },
  {
    ngx_string("CQuick_pushQueue"),
    NGX_HTTP_LOC_CONF|NGX_HTTP_LIF_CONF|NGX_CONF_TAKE1,
    nginx_setPushKey,
    NGX_HTTP_LOC_CONF_OFFSET,
    offsetof(ngx_pushQueue_loc_conf_t, key),
    NULL
  },
  ngx_null_command
};

static char *ngx_pushQueue_addHost(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_pushQueue_loc_conf_t *rlcf = conf;

    ngx_str_t                  *value;
    ngx_http_core_loc_conf_t   *clcf;
    ngx_uint_t                  n;
    ngx_url_t                   url;

    ngx_http_compile_complex_value_t         ccv;

    if (rlcf->upstream.upstream) {
        return "is duplicate";
    }

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_pushQueue_handler;

    if (clcf->name.data[clcf->name.len - 1] == '/') {
        clcf->auto_redirect = 1;
    }

    value = cf->args->elts;

    n = ngx_http_script_variables_count(&value[1]);
    if (n) {
        rlcf->host = ngx_palloc(cf->pool,sizeof(ngx_http_complex_value_t));
        if (rlcf->host == NULL) {
            return NGX_CONF_ERROR;
        }

        ngx_memzero(&ccv, sizeof(ngx_http_compile_complex_value_t));
        ccv.cf = cf;
        ccv.value = &value[1];
        ccv.complex_value = rlcf->host;

        if (ngx_http_compile_complex_value(&ccv) != NGX_OK) {
            return NGX_CONF_ERROR;
        }

        return NGX_CONF_OK;
    }

    rlcf->host = NULL;

    ngx_memzero(&url, sizeof(ngx_url_t));

    url.url = value[1];
    url.no_resolve = 1;

    rlcf->upstream.upstream = ngx_http_upstream_add(cf, &url, 0);
    if (rlcf->upstream.upstream == NULL) {
        return NGX_CONF_ERROR;
    }

    return NGX_CONF_OK;
}



static void *ngx_pushQueue_create_loc_conf(ngx_conf_t *cf)
{
    ngx_pushQueue_loc_conf_t *conf;
    conf = ngx_pcalloc(cf->pool, sizeof(ngx_pushQueue_loc_conf_t));
    if(conf == NULL) {
       return NGX_CONF_ERROR;
    }
    conf->mqType.len = 0;
    conf->mqType.data = NULL;
    conf->rabbitHost.len = 0;
    conf->rabbitHost.data = NULL;
    conf->host = NULL;
    conf->rabbitSocket = NULL;
    conf->rabbitExchange.len = 0;
    conf->rabbitExchange.data = NULL;
    conf->rabbitConn = NULL;
    conf->username.len = 0;
    conf->username.data = NULL;
    conf->isRabbitConnected = 0;
    conf->password.len = 0;
    conf->password.data = NULL;
    conf->key.len = 0;
    conf->key.data = NULL;
    conf->successTem.len = 0 ;
    conf->successTem.data = NULL;
    conf->failTem.len = 0;
    conf->failTem.data = NULL;

    conf->upstream.connect_timeout = 3000;
    conf->upstream.send_timeout = 3000;
    conf->upstream.read_timeout = 3000;
    conf->upstream.buffer_size = 1024;
    conf->upstream.cyclic_temp_file = 0;
    conf->upstream.buffering = 0;
    conf->upstream.ignore_client_abort = 1;
    conf->upstream.send_lowat = 0;
    conf->upstream.bufs.num = 0;
    conf->upstream.busy_buffers_size = 0;
    conf->upstream.max_temp_file_size = 0;
    conf->upstream.temp_file_write_size = 0;
    conf->upstream.intercept_errors = 1;
    conf->upstream.intercept_404 = 1;
    conf->upstream.pass_request_headers = 0;
    conf->upstream.pass_request_body = 0;

    return conf;
}
 
static ngx_http_module_t ngx_pushQueue_module_ctx = {
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ngx_pushQueue_create_loc_conf,
    NULL
};
 
ngx_module_t ngx_pushQueue_module = {
    NGX_MODULE_V1,
    &ngx_pushQueue_module_ctx,
    ngx_pushQueue_commands,
    NGX_HTTP_MODULE,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    ngx_pushQueue_exitProcess,
    NULL,
    NGX_MODULE_V1_PADDING
};

static char *ngx_pushQueue_getAuth(ngx_conf_t *cf, ngx_command_t *cmd, void *conf){

   ngx_str_t   *value;
   ngx_pushQueue_loc_conf_t      *config = conf;

   ngx_conf_set_str_slot(cf, cmd, conf);

   value = cf->args->elts;


   if (cf->args->nelts == 3) {
      config->username = value[1];
      config->password = value[2];
   }else if(cf->args->nelts == 2){
      config->password = value[1];
   }

   return NGX_CONF_OK;
}

static int get_error(int x, u_char const *context, u_char* error)
{
    if (x < 0) {
        syslog(LOG_ERR, "%s: %s\n", context, amqp_error_string2(x));
        ngx_sprintf(error, "%s: %s\n", context, amqp_error_string2(x));
        return 1;
    }
    return 0;
}

static int get_amqp_error(amqp_rpc_reply_t x, u_char const *context, u_char* error)
{
    switch (x.reply_type) {
        case AMQP_RESPONSE_NORMAL:
        return 0;

        case AMQP_RESPONSE_NONE:
        syslog(LOG_ERR, "%s: missing RPC reply type!", context);
        ngx_sprintf(error, "%s: missing RPC reply type!\n", context);
        break;

        case AMQP_RESPONSE_LIBRARY_EXCEPTION:
        syslog(LOG_ERR, "%s: %s", context, amqp_error_string2(x.library_error));
        ngx_sprintf(error, "%s: %s\n", context, amqp_error_string2(x.library_error));
        break;

        case AMQP_RESPONSE_SERVER_EXCEPTION:
        switch (x.reply.id) {
            case AMQP_CONNECTION_CLOSE_METHOD: {
                amqp_connection_close_t *m = (amqp_connection_close_t *) x.reply.decoded;
                syslog(LOG_ERR, "%s: server connection error %d, message: %.*s",
                    context,
                    m->reply_code,
                    (int) m->reply_text.len, (char *) m->reply_text.bytes);
                ngx_sprintf(error, "%s: server connection error %d, message: %.*s\n",
                    context,
                    m->reply_code,
                    (int) m->reply_text.len, (char *) m->reply_text.bytes);
                break;
            }
            case AMQP_CHANNEL_CLOSE_METHOD: {
                amqp_channel_close_t *m = (amqp_channel_close_t *) x.reply.decoded;
                syslog(LOG_ERR,  "%s: server channel error %d, message: %.*s",
                    context,
                    m->reply_code,
                    (int) m->reply_text.len, (char *) m->reply_text.bytes);
                ngx_sprintf(error, "%s: server channel error %d, message: %.*s\n",
                    context,
                    m->reply_code,
                    (int) m->reply_text.len, (char *) m->reply_text.bytes);
                break;
            }
            default:
            syslog(LOG_ERR, "%s: unknown server error, method id 0x%08X", context, x.reply.id);
            ngx_sprintf(error, "%s: unknown server error, method id 0x%08X\n", context, x.reply.id);
            break;
        }
        break;
    }

    return 1;
}


static ngx_int_t ngx_pushQueue_createRequest(ngx_http_request_t *r){

    ngx_buf_t                       *b;
    ngx_chain_t                     *cl;

    ngx_pushQueue_loc_conf_t      *config;
    config = ngx_http_get_module_loc_conf(r,ngx_pushQueue_module);

    char *putData = NULL;
    putData = getMessageContent(r);

    char key[100] = {0};
    memcpy(key,config->key.data,config->key.len);
	
	if(config->key.len > 100){
		return NGX_ERROR;
	}

    int needLen = strlen(putData) + 150;
    char *command = malloc(needLen);
    memset(command,0,needLen);

    sprintf(command,"*3\r\n$5\r\nrpush\r\n$%ld\r\n%s\r\n$%ld\r\n%s\r\n",strlen(key),key,strlen(putData),putData);

    int commandLen = strlen(command)+1;
    b = ngx_create_temp_buf(r->pool,commandLen);  
    b->last = b->pos + commandLen;
    //ngx_snprintf(b->pos,commandLen,command,&r->args);  
	ngx_memcpy(b->pos, command, commandLen);  

    free(putData);
    free(command);

    cl = ngx_alloc_chain_link(r->pool);
    if (cl == NULL) {
        return NGX_ERROR;
    }

    cl->buf = b;
    cl->next = NULL;

    r->upstream->request_bufs = cl;

    return NGX_OK;

}


static void ngx_pushQueue_finalizeRequest(ngx_http_request_t *r, ngx_int_t rc){
    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        r->headers_out.status = rc;
    }
    return;
}

static ngx_int_t ngx_pushQueue_processReply(ngx_pushQueue_ctx_t *ctx, ssize_t bytes)
{

    ngx_http_upstream_t      *u;

    u = ctx->request->upstream;
    u->keepalive = 1;
    u->length = 0;
    u->headers_in.status_n = NGX_HTTP_OK;
    u->state->status = NGX_HTTP_OK;
    showSuccessMessage(ctx->request,"{}");   
 
    return NGX_OK;
}

static ngx_int_t ngx_pushQueue_filterInit(void *data)
{

    return NGX_OK;
}


static ngx_int_t ngx_pushQueue_filter(void *data, ssize_t bytes)
{
    ngx_pushQueue_ctx_t  *ctx = data;

    return ctx->filter(ctx, bytes);
    
}

static ngx_int_t ngx_pushQueue_processHeader(ngx_http_request_t *r)
{

    ngx_http_upstream_t         *u;
    ngx_buf_t                   *b;
    u_char                       chr;
    ngx_str_t                    buf;
    ngx_pushQueue_ctx_t       *ctx;

    u = r->upstream;
    b = &u->buffer;

    if (b->last - b->pos < (ssize_t) sizeof(u_char)) {
        return NGX_AGAIN;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_pushQueue_module);

    chr = *b->pos;

    switch (chr) {
        case '+':
        case '-':
        case ':':
        case '$':
        case '*':
            ctx->filter = ngx_pushQueue_processReply;
            break;

        default:
            buf.data = b->pos;
            buf.len = b->last - b->pos;
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"redis sent invalid response: \"%V\"", &buf);
            return NGX_HTTP_UPSTREAM_INVALID_HEADER;
    }

    return NGX_OK;
}

static void getRequestMethod(ngx_http_request_t *r,char **method){

    if(r->method == NGX_HTTP_GET){
      sprintf(*method,"GET"); 
    }else if(r->method == NGX_HTTP_HEAD){
      sprintf(*method,"HEAD");
    }else if(r->method == NGX_HTTP_POST){
      sprintf(*method,"POST");
    }else if(r->method == NGX_HTTP_PUT){
      sprintf(*method,"PUT");
    }else if(r->method == NGX_HTTP_DELETE){
      sprintf(*method,"DELETE");
    }else if(r->method == NGX_HTTP_MKCOL){
      sprintf(*method,"MKCOL");
    }else if(r->method == NGX_HTTP_COPY){
      sprintf(*method,"COPY");
    }else if(r->method == NGX_HTTP_MOVE){
      sprintf(*method,"MOVE");
    }else if(r->method == NGX_HTTP_OPTIONS){
      sprintf(*method,"OPTIONS");
    }else if(r->method == NGX_HTTP_PROPFIND){
      sprintf(*method,"PROPFIND");
    }else if(r->method == NGX_HTTP_PROPPATCH){
      sprintf(*method,"PROPPATCH");
    }else if(r->method == NGX_HTTP_LOCK){
      sprintf(*method,"LOCK");
    }else if(r->method == NGX_HTTP_UNLOCK){
      sprintf(*method,"UNLOCK");
    }else if(r->method == NGX_HTTP_TRACE){
      sprintf(*method,"TRACE");
    }else{
      sprintf(*method,"OTHER");
    }
    

}


static int str_replace(char strRes[],char from[],char to[]){
    int i,flag = 0;
    char *p,*q,*ts;
    for(i = 0 ; strRes[i];++i){
      if(strRes[i] == from[0]){
        p = strRes + i;
        q = from;
        while(*q && (*p++ == *q++));
        if(*q == '\0'){
          ts = (char*)malloc(strlen(strRes) + 1);
          strcpy(ts,p);
          strRes[i] = '\0';
          strcat(strRes,to);
          strcat(strRes,ts);
          free(ts);
          flag = 1;
        }
      }
    }
    return flag;
}

static char *getErrorTips(ngx_http_request_t *r,int type)
{

   //获取配置
   ngx_pushQueue_loc_conf_t *config;
   config = ngx_http_get_module_loc_conf(r,ngx_pushQueue_module);

   char tips[10240] = {0};
   char error[512] = {0};
   if(type == 1){
      sprintf(error,"call CQuick_pushQueue must set CQuick_mqType,like CQuick_mqType redis");
   }else if(type == 2){
      sprintf(error,"call CQuick_pushQueue must set CQuick_pass,like CQuick_pass 127.0.0.1:6379");
   }else if(type == 3){
      sprintf(error,"call CQuick_pushQueue must set the mq key,like CQuick_pushQueue pushList");
   }else if(type == 4){
      sprintf(error,"can not connect to MessageQueue");
   }else if(type == 5){
      sprintf(error,"publish to MessageQueue error");
   }else if(type == 6){
      sprintf(error,"publish to MessageQueue error,host format error");
   }else if(type == 7){
      sprintf(error,"publish to MessageQueue error,can not create socket");
   }else if(type == 8){
      sprintf(error,"publish to MessageQueue error,can not open socket");
   }else if(type == 9){
      sprintf(error,"publish to MessageQueue error,cant not login to MQ");
   }else if(type == 10){
      sprintf(error,"publish to MessageQueue error,can not open channel");
   }else{
      sprintf(error,"unknow error");
   }


   if(config->failTem.len > 0 && config->failTem.len < 10000){
      memcpy(tips,config->failTem.data,config->failTem.len);
      str_replace(tips,"$tips",error);
   }else{
      sprintf(tips,"{\"result\":false,\"data\":{},\"error\":{\"id\":%d,\"message\":\"%s\"}}",type,error);
   }

   return strdup(tips);
   
}

static ngx_int_t showErrorMessage(ngx_http_request_t *r,int type){
    ngx_int_t rc = ngx_http_discard_request_body(r);
    if (rc != NGX_OK) {
        return rc;
    }

    char *errorTips = getErrorTips(r,type);

    ngx_chain_t out;
    u_char output[10240] = {0};
    ngx_sprintf(output, "%s",errorTips);
    free(errorTips);

    ngx_uint_t content_length = ngx_strlen(output);

    ngx_buf_t *b;
    b = ngx_pcalloc( r->pool, sizeof(ngx_buf_t) );
    out.buf = b;
    out.next = NULL;

    b->pos = output;
    b->last = output + content_length;
    b->memory = 1;
    b->last_buf = 1;

    //ngx_str_set( &r->headers_out.content_type, "text/html" );
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = content_length;
    rc = ngx_http_send_header( r );   
    ngx_int_t p = ngx_http_output_filter(r, &out);
    ngx_http_finalize_request(r,p);
    return p;
}

static ngx_int_t showSuccessMessage(ngx_http_request_t *r,char *message){

    ngx_pushQueue_loc_conf_t      *config;
    config = ngx_http_get_module_loc_conf(r,ngx_pushQueue_module);
    u_char output[10240] = {0};    
    if(config->successTem.len > 0 && config->successTem.len < 10240){
        memcpy(output,config->successTem.data,config->successTem.len);
    }else{
        ngx_sprintf(output, "{\"result\":true,\"data\":%s,\"error\":{\"id\":0,\"message\":\"\"}}",message);   
    }

    ngx_chain_t out;
    ngx_uint_t content_length = ngx_strlen(output);
    ngx_buf_t *b;
    b = ngx_pcalloc( r->pool, sizeof(ngx_buf_t) );
    out.buf = b;
    out.next = NULL;

    b->pos = output;
    b->last = output + content_length;
    b->memory = 1;
    b->last_buf = 1;
    //ngx_str_set( &r->headers_out.content_type, "text/html" );
    r->headers_out.status = NGX_HTTP_OK;
    r->headers_out.content_length_n = content_length;
    ngx_http_send_header(r);
    ngx_int_t p = ngx_http_output_filter(r, &out);
    //ngx_http_finalize_request(r,p);
    return p;
}


static char *getMessageContent(ngx_http_request_t *r){

    ngx_pushQueue_loc_conf_t      *config;

    config = ngx_http_get_module_loc_conf(r, ngx_pushQueue_module);   

    char queryParams[10240] = {0};
    memcpy(queryParams, r->args.data, r->args.len);

    char *requestMethod = malloc(20);
    getRequestMethod(r,&requestMethod);

    char uri[4048] = {0};
    memcpy(uri, r->uri.data, r->uri.len);

    char myType[128] = {0};
    memcpy(myType,config->mqType.data,config->mqType.len);

    char key[128] = {0};
    memcpy(key,config->key.data,config->key.len);

    char ip[60] = {0};
    memcpy(ip,r->connection->addr_text.data,r->connection->addr_text.len);

    time_t now;
    now = ngx_time();

    char *putData = NULL;

    if (NULL == r->headers_in.content_length || 0 == atoi((const char *)r->headers_in.content_length->value.data))
    {
          putData = malloc(strlen(queryParams) + 1024);
          memset(putData,0,strlen(queryParams) + 1024);
          sprintf(putData, "{\"time\":%ld,\"ip\":\"%s\",\"query\":\"%s\",\"content\":\"\",\"method\":\"%s\",\"uri\":\"%s\",\"queryLen\":%ld,\"mqType\":\"%s\"}",now,ip,queryParams,requestMethod,uri,strlen(queryParams),myType);
    }else{

      size_t len = atoi((const char *)r->headers_in.content_length->value.data);
      char *postData = malloc(len+10);

      putData = malloc(len+1024+strlen(queryParams));
      memset(putData,0,len+1024+strlen(queryParams));
      memset(postData, 0, len+10);
      size_t slen = 0;
      ngx_chain_t *bufs = r->request_body->bufs;

      while(bufs)
      {
         ngx_buf_t *buf = bufs->buf;
         char *thisString = malloc(buf->last - buf->pos + 1);
         memcpy(thisString,buf->pos,buf->last - buf->pos);
         sprintf(postData,"%s%s",postData,thisString);
         free(thisString);
         slen += (buf->last - buf->pos);
         bufs = bufs->next;
      }


      sprintf(putData, "{\"time\":%ld,\"ip\":\"%s\",\"query\":\"%s\",\"content\":\"%s\",\"method\":\"%s\",\"uri\":\"%s\",\"queryLen\":%ld,\"mqType\":\"%s\"}",now,ip,queryParams,postData,requestMethod,uri,strlen(queryParams),myType);
      free(postData);
    }

    free(requestMethod);
    return putData;
}

ngx_http_upstream_srv_conf_t * ngx_pushQueue_addUpstream(ngx_http_request_t *r, ngx_url_t *url)
{
    ngx_http_upstream_main_conf_t  *umcf;
    ngx_http_upstream_srv_conf_t  **uscfp;
    ngx_uint_t                      i;

    umcf = ngx_http_get_module_main_conf(r, ngx_http_upstream_module);

    uscfp = umcf->upstreams.elts;

    for (i = 0; i < umcf->upstreams.nelts; i++) {

        ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"now host %s == %d", uscfp[i]->host.data,uscfp[i]->port);

        if (uscfp[i]->host.len != url->host.len || ngx_strncasecmp(uscfp[i]->host.data, url->host.data,url->host.len) != 0)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log,0,"upstream_add: host not match");
            continue;
        }

        if (uscfp[i]->port != url->port) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log,0,"upstream_add: port not match: %d != %d",(int) uscfp[i]->port, (int) url->port);
            continue;
        }

#if (nginx_version < 1011006)
        if (uscfp[i]->default_port
            && url->default_port
            && uscfp[i]->default_port != url->default_port)
        {
            ngx_log_error(NGX_LOG_ERR, r->connection->log,0,"upstream_add: default_port not match");
            continue;
        }
#endif

        return uscfp[i];
    }

    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"no upstream found: %.*s", (int) url->host.len, url->host.data);

    return NULL;
}

static int connect_amqp(ngx_http_request_t *r,ngx_pushQueue_loc_conf_t *config){

    char host[100] = {0};
    memcpy(host,config->rabbitHost.data,config->rabbitHost.len);
   
    int ip1,ip2,ip3,ip4,port;
    if(5 != sscanf(host,"%d.%d.%d.%d:%d",&ip1,&ip2,&ip3,&ip4,&port)){
        config->isRabbitConnected = -6;
        return -6;
    }

    amqp_rpc_reply_t reply;
    config->rabbitConn = amqp_new_connection();
    config->rabbitSocket = amqp_tcp_socket_new(config->rabbitConn);
    if(!config->rabbitSocket){
        config->isRabbitConnected = -7;
        return -7;
    }
    int status = 0;
    char ip[60];
    sprintf(ip,"%d.%d.%d.%d",ip1,ip2,ip3,ip4);
    status = amqp_socket_open(config->rabbitSocket, ip,port);
    if(status){
        config->isRabbitConnected = -8;
        return -8;
    }

    reply = amqp_login(config->rabbitConn, "/", 0, 131072, 0, AMQP_SASL_METHOD_PLAIN,(char*)config->username.data,(char*)config->password.data);
    if (AMQP_RESPONSE_NORMAL != reply.reply_type){
        config->isRabbitConnected = -9;
        return -9;
    }

    amqp_channel_open(config->rabbitConn, 1);
    u_char *error;
    error = ngx_pcalloc(r->pool, 32768);
    if(get_amqp_error(amqp_get_rpc_reply(config->rabbitConn), (u_char*)"Opening channel", error)){
        config->isRabbitConnected = -10;
        return -10;
    }

    config->isRabbitConnected = 1;
    return 1;
}

static void pushMessageToRabbit(ngx_http_request_t *r){
    
    ngx_pushQueue_loc_conf_t      *config;

    config = ngx_http_get_module_loc_conf(r, ngx_pushQueue_module);

    if(config->isRabbitConnected != 1){	
        connect_amqp(r,config);        
    }

   if(config->isRabbitConnected != 1){
       showErrorMessage(r,0 - config->isRabbitConnected);
       return;
   }

   //生成保存参数
   char *putData = NULL;
   putData = getMessageContent(r);
  
   u_char                   *msg;
   amqp_basic_properties_t  props;
   msg = ngx_pcalloc(r->pool, 32768);

   props._flags = AMQP_BASIC_CONTENT_TYPE_FLAG | AMQP_BASIC_DELIVERY_MODE_FLAG;
   props.content_type = amqp_cstring_bytes("text/json");
   props.delivery_mode = 2;

   if(get_error(amqp_basic_publish(config->rabbitConn, 1, amqp_cstring_bytes((char*)config->rabbitExchange.data), amqp_cstring_bytes((char*)config->key.data), 0, 0, &props, amqp_cstring_bytes(putData)), (u_char*)"Publishing", msg)){

       char rabbitHost[256] = {0};
       memcpy(rabbitHost,config->rabbitHost.data,config->rabbitHost.len);
       ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"push message to rabbit fail , try to reconnect the server %s",rabbitHost);

       //重新连接socket再试一次
       if(connect_amqp(r, config) <= 0){
          showErrorMessage(r,4);
          free(putData);
          ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"push message to rabbit fail , try to reconnect the server %s ,can not connect to server",rabbitHost);
          return;
       }

       if(get_error(amqp_basic_publish(config->rabbitConn, 1, amqp_cstring_bytes((char*)config->rabbitExchange.data), amqp_cstring_bytes((char*)config->key.data), 0, 0, &props, amqp_cstring_bytes(putData)), (u_char*)"Publishing", msg)){
          showErrorMessage(r,5);
          free(putData);
          ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"push message to rabbit fail , reconnect the server %s ,but still push fail ",rabbitHost);
          return;
       }
   }    

   free(putData);
   showSuccessMessage(r,"{}");
   ngx_http_finalize_request(r, ngx_http_send_header(r));
}

static ngx_int_t ngx_pushQueue_handler(ngx_http_request_t *r) {

    ngx_int_t                        rc;
    ngx_http_upstream_t             *u;
    ngx_pushQueue_ctx_t           *ctx;
    ngx_pushQueue_loc_conf_t      *rlcf;
    ngx_str_t                        target;
    ngx_url_t                        url;


    if (ngx_http_upstream_create(r) != NGX_OK) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    u = r->upstream;

    rlcf = ngx_http_get_module_loc_conf(r, ngx_pushQueue_module);


    char mqType[100] = {0};
    memcpy(mqType,rlcf->mqType.data,rlcf->mqType.len);

    if(strcmp(mqType,"rabbitMQ") == 0){
       rc = ngx_http_read_client_request_body(r, pushMessageToRabbit);
       if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
          return rc;
       }
       return NGX_DONE;
    }
 

    if (rlcf->host) {
        if (ngx_http_complex_value(r, rlcf->host, &target) != NGX_OK){
            return NGX_ERROR;
        }

        if(rlcf->host == NULL){
    	    ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"redis parse CQuick_pass: upstream not found");
        }

        url.host = target;
        url.port = 0;
        url.no_resolve = 1;

        rlcf->upstream.upstream = ngx_pushQueue_addUpstream(r, &url);

        if (rlcf->upstream.upstream == NULL) {
            ngx_log_error(NGX_LOG_ERR, r->connection->log, 0,"redis: upstream \"%V\" not found", &target);
            return NGX_HTTP_INTERNAL_SERVER_ERROR;
        }
    }

    ngx_str_set(&u->schema, "redis://");
    u->output.tag = (ngx_buf_tag_t) &ngx_pushQueue_module;

    //不转发upstrame内容给客户端
    u->conf = &rlcf->upstream;
    u->create_request = ngx_pushQueue_createRequest;
    u->process_header = ngx_pushQueue_processHeader;
    u->finalize_request = ngx_pushQueue_finalizeRequest;

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_pushQueue_ctx_t));
    if (ctx == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    ctx->request = r;
    ctx->state = NGX_OK;
    ngx_http_set_ctx(r, ctx, ngx_pushQueue_module);

    u->input_filter_init = ngx_pushQueue_filterInit;
    u->input_filter = ngx_pushQueue_filter;
    u->input_filter_ctx = ctx;

    rc = ngx_http_read_client_request_body(r, ngx_http_upstream_init);

    if (rc >= NGX_HTTP_SPECIAL_RESPONSE) {
        return rc;
    }

    return NGX_DONE;

}

static void ngx_pushQueue_exitProcess(ngx_cycle_t* cycle){

     ngx_pushQueue_loc_conf_t *config=(ngx_pushQueue_loc_conf_t*)ngx_get_conf(cycle->conf_ctx, ngx_pushQueue_module);
     if(config->rabbitConn != NULL){
         amqp_channel_close(config->rabbitConn, 1, AMQP_REPLY_SUCCESS);
         amqp_connection_close(config->rabbitConn, AMQP_REPLY_SUCCESS);
         amqp_destroy_connection(config->rabbitConn);
     }
}

