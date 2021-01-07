# ngx_pushQueue_module

ngx_pushQueue_module is used to add HTTP request information to rabitMQ queue or redis list during HTTP request.

<h2>外部库依赖</h2>
<p>需安装<a href="https://github.com/alanxz/rabbitmq-c">rabbitmq-c库</a>方可编译此扩展</p>
<p>安装后启动nginx若提示找不到rabbitmq.so 可执行 ldd $(which /usr/local/nginx/sbin/nginx) 查看nginx加载的lib目录，移动一份到对应的目录<p>

<h2>使用方法</h2>
<h3>将HTTP请求加入Redis List</h3>
<pre>

    server{
    
       ...
       http{
          ...
          location /redis/ {
               CQuick_mqType redis;         #指定放入的后端存储类型  支持 redis/rabbitMQ
               CQuick_pass redis2_ups;      #可指定为upstream或直接配置如127.0.0.1:6379 与下面这行二选一即可
               CQuick_pass 127.0.0.1:6379;  #可指定为upstream或直接配置如127.0.0.1:6379 与上面这行二选一即可
               CQuick_pushQueue testQueue;  #指定放入的redis list键 此扩展将会把http信息  rpush到testQueue键中
               
               #以下两行为可选配置，定义http请求的返回模板，不配置时将固定返回一个JSON
               CQuick_successTemplate "{\"result\":true,\"data\":{},\"error\":{\"id\":0,\"message\":\"\"}}";
               CQuick_failTemplate "{\"result\":false,\"data\":{},\"error\":{\"id\":1,\"message\":\"$tips\"}}";
           }
       }
  
       upstream redis_ups{
           server 127.0.0.1:6379;        
           keepalive 1024;                  #启用keepalive将保持与redis的连接
       }
  
    }
</pre>

<h3>将HTTP请求加入RabbitMQ</h3>
<pre>

    server{
    
       ...
       http{
          ...
          location /rabbit/ {
               CQuick_mqType rabbitMQ;                 #指定放入的后端存储类型  支持 redis/rabbitMQ
               CQuick_rabbitHost 127.0.0.1:5672;       #rabbit服务器IP 端口
               CQuick_auth username password;          #rabbit服务器 账号/密码
               CQuick_rabbitExchange testExchange;     #rabbit定义的exchange 交换器            
               CQuick_pushQueue testRouter;            #rabbit publish的目标路由
               
               #以下两行为可选配置，定义http请求的返回模板，不配置时将固定返回一个JSON
               CQuick_successTemplate "{\"result\":true,\"data\":{},\"error\":{\"id\":0,\"message\":\"\"}}";
               CQuick_failTemplate "{\"result\":false,\"data\":{},\"error\":{\"id\":1,\"message\":\"$tips\"}}";
           }
       }
    }
</pre>

<h3>推送的数据格式</h3>
此扩展会将HTTP主要信息 如请求方式、请求IP、时间、GET参数、POST参数等组织成一个JSON放入到指定的队列中。
数据格式示例
<pre>
    {
        "mqType": "rabbitMQ",
        "uri": "/hello_world/",
        "method": "GET",
	"time": 1609992360,
	"ip": "118.113.14.162",
	"query": "id=123&test=123&name=123",
        "queryLen": 24,
	"content": ""
    }
</pre>
mqType：存储队列类型，[redis|rabbitMQ]<br>
uri:此消息对应请求的URI<br>
method：请求方式 [GET/POST/PUT/HEAD]<br>
time:请求时间，10位时间戳<br>
ip:此请求连接方IP，如果请求经过反向代理或其他转发，此IP可能不能代表用户真实IP<br>
query：请求URL中的参数<br>
queryLen：URL参数长度<br>
content：POST请求时，此字段代表POST的数据内容<br>
