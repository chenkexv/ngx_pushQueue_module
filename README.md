# ngx_pushQueue_module

ngx_pushQueue_module is used to add HTTP request information to rabitMQ queue or redis list during HTTP request.

<h2>安装与依赖</h2>
<p>加入请求到Redis是使用nginx提供的upstream机制，不依赖外部库，加入请求到Rabbit依赖<a href="https://github.com/alanxz/rabbitmq-c">rabbitmq-c库</a></p>
<p>1.安装rabbitmq-c</p>
<p>&nbsp;&nbsp;&nbsp;&nbsp;1.</p>
