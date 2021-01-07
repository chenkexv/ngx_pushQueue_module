# ngx_pushQueue_module

ngx_pushQueue_module is used to add HTTP request information to rabitMQ queue or redis list during HTTP request.

<h2>外部库依赖</h2>
<p>需安装<a href="https://github.com/alanxz/rabbitmq-c">rabbitmq-c库</a>方可编译此扩展</p>
<p>推送请求到Redis是使用nginx提供的upstream机制，不依赖外部库<p>
<p>推送请求到Rabbit依赖<a href="https://github.com/alanxz/rabbitmq-c">rabbitmq-c库</a></p>

