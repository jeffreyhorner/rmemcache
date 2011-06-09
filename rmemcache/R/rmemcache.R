mcConnect <- function(servers=NULL,hashfun=NULL) {# servers=c("127.0.0.1:11211"))
		mcon <- .Call("mc_connect",servers,hashfun,PACKAGE="rmemcache")
		class(mcon) <- c(class(mcon),"rmemcache.con")
		mcon
}

print.rmemcache.con <- function(mcon,...)
	.Call("mc_print_con",mcon,PACKAGE="rmemcache")

mcSetservers <- function(mcon,servers=NULL)
		.Call("mc_setservers",mcon,servers,PACKAGE="rmemcache")
mcHashfun <- function(mcon,fun=NULL)
		.Call("mc_hashfun",mcon,fun,PACKAGE="rmemcache")
mcHash <- function(mcon, key=NULL)
		.Call("mc_hash",mcon,key,PACKAGE="rmemcache")
#mcCompress <- function(mcon,threshold=0)
mcAdd <- function(mcon,key,value,exptime=0,counter=FALSE){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_store",mcon,key,value,as.integer(exptime),"add",PACKAGE="rmemcache")
}
mcSet <- function(mcon,key,value,exptime=0,counter=FALSE){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_store",mcon,key,value,as.integer(exptime),"set",PACKAGE="rmemcache")
}
mcReplace <- function(mcon,key,value,exptime=0,counter=FALSE){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_store",mcon,key,value,as.integer(exptime),"replace",PACKAGE="rmemcache")
}
mcGet <- function(mcon,key){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_get",mcon,key,PACKAGE="rmemcache")
}
mcDelete <- function(mcon,key,noReply=FALSE){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_delete",mcon,key,noReply,PACKAGE="rmemcache")
}
mcIncr <- function(mcon,key,byvalue){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_incr",mcon,key,as.integer(byvalue),PACKAGE="rmemcache")
}
mcDecr <- function(mcon,key,byvalue){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_descr",mcon,key,as.integer(byvalue),PACKAGE="rmemcache")
}
mcStats <- function(mcon,args=NULL){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_stats",mcon,args,PACKAGE="rmemcache")
}
mcFlushall <- function(mcon,exptime=0){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_flushall",mcon,as.integer(exptime),PACKAGE="rmemcache")
}
mcVersion <- function(mcon){
#	    on.exit(.Call("mc_destroy_iobufs",mcon,PACKAGE="rmemcache"))
		.Call("mc_version",mcon,PACKAGE="rmemcache")
}
mcDisconnect <- function(mcon)
		.Call("mc_disconnect",mcon,PACKAGE="rmemcache")
