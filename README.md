# ngx_pushQueue_module

ngx_pushQueue_module is used to add HTTP request information to rabitMQ queue or redis list during HTTP request.

<h2>外部库依赖</h2>
<p>需安装<a href="https://github.com/alanxz/rabbitmq-c">rabbitmq-c库</a>方可编译此扩展</p>

<h2>使用方法</h2>
<h3>1.将HTTP请求加入Redis List</h3>
<code>

    server{
    
       ...
       http{
          ...
          location /redis/ {
               CQuick_mqType redis;         //指定放入的后端存储类型  支持 redis/rabbitMQ
               CQuick_pass redis2_ups;      //可指定为upstream或直接配置如127.0.0.1:6379
               CQuick_pass 127.0.0.1:6379;  //可指定为upstream或直接配置如127.0.0.1:6379
               CQuick_pushQueue testQueue;  //指定放入的redis list键 此扩展将会把http信息  rpush到testQueue键中
           }
       }
  
       upstream redis_ups{
           server 127.0.0.1:6379;        
           keepalive 1024;                  //启用keepalive将保持与redis的连接
       }
  
    }
</code>
