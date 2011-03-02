#include <stdlib.h>
#include <setjmp.h>
#include <errno.h>

#include <R.h>
#include <Rinternals.h>
#include <R_ext/Callbacks.h>
#include <R_ext/Rdynload.h>
#include "sock.h"

static SEXP MCCON_type_tag;

typedef struct {
	int scon;
	int port;
	char *host;
} mc_srv;

typedef struct {
	unsigned char *buf;
	jmp_buf jmp_env;
	size_t size;
	size_t count;
	size_t curpos;
} mc_buf;

/* Default buffer sizes are expressed as a power of two */
#define MC_DEFAULT_POW_TWO 12

typedef struct {
	int nservers;
	mc_srv **servers;
	int threshold;
	SEXP hashfun;
	mc_buf *ibuf;
	mc_buf *obuf;
} mc_con;

/* Prototypes */
SEXP mc_hashfun(SEXP mcon_s, SEXP hashfun);

static mc_con *unmarshall_con(SEXP mcon_s){
	mc_con *mcon;
	if (TYPEOF(mcon_s) != EXTPTRSXP ||
			R_ExternalPtrTag(mcon_s) != MCCON_type_tag){
		error("rmemcache: SEXP not an mcon");
		return NULL;
	}
	mcon = R_ExternalPtrAddr(mcon_s);
	if (mcon == NULL){
		error("rmemcache: SEXP not an mcon");
		return NULL;
	}
	return mcon;
}

static int close_sockets(mc_con *mcon){
	if (mcon->servers != NULL) {
		int i;
		for (i=0;i<mcon->nservers;i++){
			if (mcon->servers[i]->scon != -1) mc_SockClose(mcon->servers[i]->scon);
		}
	}
	return TRUE;
}

static void destroy_srvlist(mc_con *mcon){
	if (mcon->servers != NULL) {
		close_sockets(mcon);
		int i;
		for (i=0;i<mcon->nservers;i++){
			free(mcon->servers[i]);
		}
		free(mcon->servers);
		mcon->nservers=0;
	}
}

static void destroy_iobufs( mc_con *mcon){
	if (mcon != NULL) {
		if (mcon->obuf) {
			if (mcon->obuf->buf) free(mcon->obuf->buf);
			free(mcon->obuf);
			mcon->obuf = NULL;
		}
		if (mcon->ibuf) {
			if (mcon->ibuf->buf) free(mcon->ibuf->buf);
			free(mcon->ibuf);
			mcon->ibuf = NULL;
		}
	}
}

SEXP mc_destroy_iobufs( SEXP mcon_s ){
	mc_con *mcon = unmarshall_con(mcon_s);
	if (mcon) destroy_iobufs(mcon);
	return R_NilValue;
}

void mc_finalize_con(SEXP mcon_s){
	mc_con *mcon = unmarshall_con(mcon_s);
	if (mcon == NULL) return;

	destroy_srvlist(mcon);
	if (mcon->hashfun) R_ReleaseObject(mcon->hashfun);
	destroy_iobufs(mcon);
	free(mcon);
}

static int populate_srvlist(mc_con **p_mcon,SEXP srvlist){
	mc_con *mcon = *p_mcon;
	mc_srv *srv;
	int i, nservers;
	char *colon;
	char *ip, *port;

	if (!isString(srvlist)){
		warning("rmemcache: server list must be a character vector!");
		return FALSE;
	}

	/* First pass thru list to see if all servers pass muster
	 * just checking for ip/dns and port
	 */
	nservers = LENGTH(srvlist);
	for (i = 0; i < nservers; i++){
		colon = strchr(CHAR(STRING_PTR(srvlist)[i]),':');
		if (colon == NULL){
			warning("rmemcache: server and port must be separated by a ':'");
			return FALSE ;
		}
	}

	/* Now really allocate the server list */
	destroy_srvlist(mcon);
	mcon->servers = calloc(nservers,sizeof(mc_srv *));
	for (i = 0; i < nservers; i++){
		mcon->servers[i] = calloc(1,sizeof(mc_srv));
		mcon->servers[i]->host = strdup(CHAR(STRING_ELT(srvlist,i)));
		colon = strchr(mcon->servers[i]->host,':');
		mcon->servers[i]->port = atoi(colon+1);
		*colon = '\0';
		mcon->servers[i]->scon = -1; /* for not open; 0 is a valid socket */
	}
	mcon->nservers = nservers;

	return TRUE; /* success */
}

SEXP mc_print_con(SEXP mcon_s){
	mc_con *mcon = unmarshall_con(mcon_s);

	if (!mcon) return R_NilValue;

	Rprintf("cmpthresh: %d\n",mcon->threshold);
	if (mcon->hashfun)
		Rprintf("hashfun: user-provided\n");
	else 
		Rprintf("hashfun: internal\n");

	if (mcon->nservers){
		int i =0;
		for (i = 0; i < mcon->nservers; i++){
			Rprintf("server %d: %s %d\n",i+1,mcon->servers[i]->host,mcon->servers[i]->port);
		}
	} else {
		Rprintf("servers: 0\n");
	}

	if (mcon->obuf){
		Rprintf("obuf->count: %d\n",mcon->obuf->count);
		Rprintf("obuf->size: %d\n",mcon->obuf->size);
	}

	if (mcon->ibuf){
		Rprintf("ibuf->count: %d\n",mcon->ibuf->count);
		Rprintf("ibuf->size: %d\n",mcon->ibuf->size);
	}

	return R_NilValue;
}

SEXP mc_connect(SEXP srvlist, SEXP hashfun){
	mc_con *mcon;
	SEXP mcon_s;

	mcon = calloc(1,sizeof(mc_con));

	PROTECT(mcon_s = R_MakeExternalPtr(mcon,MCCON_type_tag,R_NilValue));
	R_RegisterCFinalizer(mcon_s,mc_finalize_con);
	UNPROTECT(1);

	if (!isNull(srvlist)){
		populate_srvlist(&mcon,srvlist);
	}

	mc_hashfun(mcon_s,hashfun);

	return mcon_s;
}

SEXP mc_setservers(SEXP mcon_s, SEXP srvlist){
	mc_con *mcon = unmarshall_con(mcon_s);
	if (!mcon) return ScalarLogical(FALSE);
	return ScalarLogical(populate_srvlist(&mcon,srvlist));
}


SEXP mc_hashfun(SEXP mcon_s, SEXP hashfun){
	mc_con *mcon = unmarshall_con(mcon_s);
	if (!mcon) return ScalarLogical(FALSE);

	if (isFunction(hashfun)){
		if (mcon->hashfun) R_ReleaseObject(mcon->hashfun);
		R_PreserveObject(hashfun);
		mcon->hashfun=hashfun;
	} else if (isNull(hashfun)){
		if (mcon->hashfun) R_ReleaseObject(mcon->hashfun);
		mcon->hashfun = NULL;
	}

	return ScalarLogical(TRUE);
}

static int hash_string(const char *s)
{
	const char *p;
	unsigned h = 0, g;
	for (p = s; *p; p = p + 1) {
		h = (h << 4) + (*p);
		if ((g = h & 0xf0000000) != 0) {
			h = h ^ (g >> 24);
			h = h ^ g;
		}
	}
	return h;
}

/* 0 based indexing */
static int hash_servers(mc_con *mcon, SEXP key){
	int h;
	char *pkey;

	/* Call user-defined hash function 
	 * must return value between 0 and nservers-1 
	 * 0 based indexing
	 */
	if (mcon->hashfun){
		SEXP val, expr, nservers;
		int error = 1;
		/* Setup nservers */
		PROTECT(nservers = allocVector(INTSXP,1));
		INTEGER(nservers)[0] = mcon->nservers;

		/* Setup call */
		PROTECT(expr = allocVector(LANGSXP,4));
		SETCAR(expr,mcon->hashfun);
		SETCAR(CDR(expr),key);
		SETCAR(CDR(CDR(expr)),nservers);

		val = R_tryEval(expr,NULL,&error);

		UNPROTECT( 2 );

		if (error){
			warning("rmemcache: hash function failed!");
			return -1;
		}

		if (isInteger(val) || isNumeric(val)){
			h = asInteger(val);
			return (h >=0 && h < mcon->nservers)? h : -1;
		}
		else {
			warning("rmemcache: hash function needs to return an integer!");
			return -1;
		}
	}

	/* Otherwise fall back to internal hash function 
	 * equal weight to servers
	 * 0 based indexing
	 */
	h = hash_string(CHAR(STRING_ELT(key,0)));

	return h % mcon->nservers;

}

SEXP mc_hash(SEXP mcon_s, SEXP key){
	int h;
	mc_con *mcon = unmarshall_con(mcon_s);
	if (!mcon) return R_NilValue;

	h = hash_servers(mcon,key);

	if (h == -1) return R_NilValue;
	return ScalarInteger(h);
}

/* round n to next power of two, 
 * minimum n will be 2**pow
 */
static int round_power_two(int pow, int n){
	int result = 1<<pow;
	while (result < n) {
		result <<= 1;
		if (result <= 0)
			return -1;
	}
	return result;
}

static mc_buf *init_buf(int size){
	int realsize;
	mc_buf *newbuf;
	realsize = round_power_two(MC_DEFAULT_POW_TWO,size);

	newbuf = calloc(1,sizeof(mc_buf));
	if (!newbuf) return NULL;

	newbuf->buf = calloc(realsize,1);
	if (!newbuf->buf) return NULL;
	newbuf->size = realsize;
	newbuf->count = 0;
	newbuf->curpos = 0;

	return newbuf;
}

/* 
 * Storage commands: add, set, replace
 *
 * client sends:
 *     <cmd> <key> <flags> <exptime> <bytes>\r\n
 *     <data>\r\n
 * server sends:
 *     STORED\r\n
 *     NOT_STORED\r\n
 *
 * The strategy for initializing this buffer is to allocate enough 
 * memory to store the first protocol line sent plus the size (vzise) of the
 * serialized  r object. vsize is just an estimate from object.size(). Then
 * we write out as much  of the protocol string that we know into the
 * buffer. this includes  "<cmd> <key> <flags> <exptime> " plus the
 * NULL byte.   Later, after we've serialized the r object to the buffer,
 * we'll go back and fill  in the <bytes> field.
 * 
 * buf->count is set to the protbuflen (guaranteed to hold the max size
 * of the first  protocol line) so that we know where the protocol line
 * ends and the serialized value begins.
 *
 * The <bytes> value is written to the address of buf + strlen(buf->buf) 
 * after  serialization.
 */
static mc_buf *init_store_buf(const char *cmd, const char *key, 
		size_t vsize, int exptime, int flags){
	mc_buf *newbuf = NULL;
	int protbuflen, buflen;

	/* We double the following to later coallesce the first protocol line
	 * with the serialzed buffer.
	 */
	protbuflen = round_power_two(1,
				(7 + 1           +     /* max size of command + 1 space */
				strlen(key) + 1  +      /* sizeof key + 1 space */
				10 + 1           +     /* 1 for flag + 1 space */
				10 + 1           +     /* max length of exptime + 1 space */
				10 + 3) * 2      );    /* max length of bytes  + \r\n and NULL */
	buflen = round_power_two(MC_DEFAULT_POW_TWO, vsize  ); /* size of R object */           

	if ((newbuf = init_buf(protbuflen + buflen)) == NULL) return NULL;

	newbuf->count = protbuflen;

	sprintf( (char *)newbuf->buf, "%s %s %d %d ", cmd, key, flags, exptime);

	return newbuf;
}
/*
 * Get 1 value command: get
 *
 * client sends:
 *     get <key>\r\n
 * server sends when key found:
 *     VALUE <key> <flags> <bytes>\r\n
 *     <data block>\r\n
 *     END\r\n
 * when key not found:
 *     END\r\n
 *
 * This buffer is initialized to "get <key>\r\n"
 * and count is set to strlen(buf->buf).
 */
static mc_buf *init_get_buf(const char *key){
	mc_buf *newbuf;
	int protbuflen;

	protbuflen = round_power_two(1,
			4               +  /* strlen("get") + 1 space */
			strlen(key) + 1 +  /* length of key + 1 space */
			2               ); /* length of \r\n */
	if ((newbuf = init_buf(protbuflen)) == NULL) return NULL;

	sprintf( (char *)newbuf->buf, "get %s\r\n", key);
	newbuf->count = strlen((char *)newbuf->buf);

	return newbuf;
}


static void resize_buf(mc_buf *buf, size_t needed)
{
	size_t newsize;

	if (needed <= buf->size) return;

	newsize = round_power_two(MC_DEFAULT_POW_TWO,needed);

    buf->buf = realloc(buf->buf,newsize);

    if (buf->buf == NULL){
		/* this causes a break to on.exit() */
		error("rmemcache: resize_buf() cannot allocate buffer");
	}
	buf->size = newsize;
}

static void outchar(R_outpstream_t stream, int c){
	mc_buf *buf = stream->data;
	if (buf->count >= buf->size)
		resize_buf(buf, buf->count + 1);
	buf->buf[buf->count++] = c;
}

static void outbytes(R_outpstream_t stream, void *bytes, int len){
	mc_buf *buf = stream->data;
	if (buf->count + len > buf->size)
		resize_buf(buf, buf->count + len);
	memcpy(buf->buf + buf->count, bytes, len);
	buf->count += len;
}

/* Turns first read newline into NULL and returns address of
 * buf->buf[buf->curpos] before buf->curpos has been incremented.
 * Will not resize buf.
 */
static unsigned char * readline_buf(mc_srv *srv,mc_buf *buf){
	int startpos, read, len;
	int i=0,j=0;

	startpos = buf->curpos;

	/* do we have a string already in the buffer */
	len = buf->curpos;
	while (len < buf->count){
		if (buf->buf[len] == '\n'){
			buf->buf[len] = '\0';
			buf->curpos = len + 1;
			return buf->buf + startpos;
		}
		len++;
	}

	/* No string, read some more from srv */
	len = 0;
	while (buf->size > buf->count){

		/* Wait for something to read */
		/* if (mc_SocketWait(srv->scon,0) != 0) return NULL; *//* timeout */

		read = mc_SockRead(srv->scon,buf->buf+buf->count,buf->size - buf->count,1);

		if (read > 0){
			len = buf->count; /* old count */
			buf->count += read;
			j=0;
			while(read--){
				if (buf->buf[len] == '\n'){
					buf->buf[len] = '\0';
					buf->curpos = len + 1;
					return buf->buf + startpos;
				} 
				len++;
			}
		} else {
		    switch(-read){
			case EAGAIN:
			case EINTR:
			    continue;
			    break;
			case EINVAL: /* server went away */
			case EBADF:
			case EFAULT: 
			default:
			    return NULL; /* error */
			    break;
		    }
		}
	}

	/* We get here if there's no string read 
	 * and our buffer is full.
	 */
	return NULL;
}

static unsigned char * readbytes_buf(mc_srv *srv, mc_buf *buf, size_t bytes){
	size_t startpos, read, len;

	startpos = buf->curpos;
	/* do we have bytes already in the buffer */
	if ((buf->count - buf->curpos) >= bytes){
		buf->curpos += bytes;
		return buf->buf + startpos;
	}

	/* Not enough, read some more from srv */
	while (buf->size > buf->count){
		read = mc_SockRead(srv->scon,buf->buf+buf->count,buf->size-buf->count,1);
		if (read){
			buf->count += read;
			/* do we have bytes already in the buffer */
			if ((buf->count - buf->curpos) >= bytes){
				buf->curpos += bytes;
				return buf->buf + startpos;
			}
		} else {
			return NULL; /* error */
		}
	}

	return NULL; /* buffer is full but still not enough bytes */
}

/* write out buf line including '\n' to srv, incrementing
 * curpos by number of bytes written.
 */
static size_t writeline_buf(mc_srv *srv, mc_buf *buf){
	size_t len = 0;

	while( (buf->curpos+len) <= buf->count){
		if (buf->buf[buf->curpos+len++] == '\n'){
			if (len == mc_SockWrite(srv->scon,buf->buf+buf->curpos,len)){
				buf->curpos += len;
				return len;
			} else {
				return 0; /* socket error */
			}
		}
	}

	return -1; /* no string found in buf*/
}

/* won't write to srv if curpos+bytes past count */
/* a return value of zero means either a sockwrite error or
 * curpos + bytes is larger than count
 */
static size_t writebytes_buf(mc_srv *srv, mc_buf *buf, size_t bytes){
	size_t ret = 0;

	if (buf->count >= (buf->curpos + bytes))
		ret = mc_SockWrite(srv->scon,buf->buf+buf->curpos,bytes);
	buf->curpos += ret;
	return ret;
}

static size_t seek_buf(mc_buf *buf, size_t s){
	if (s >= 0 && s <= buf->size){
		buf->curpos = s;
		return s;
	} else {
		return -1;
	}
}

static int error_occured(char *response){
	char *error_msg;
	if (strcmp("ERROR\r",response) == 0){
		warning("rmemcache: client sent a nonexistent command!");
		return 1;
	}
	if (strncmp("CLIENT_ERROR ",response,13) == 0){
		error_msg = response + 13;
		warning("rmemcache: %s",error_msg);
		return 1;
	}
	if (strncmp("SERVER_ERROR ",response,13) == 0){
		error_msg = response + 13;
		warning("rmemcache: %s",error_msg);
		return 1;
	}

	return 0;
}

/* 
 * Storage commands: add, set, replace
 *
 * client sends:
 *     <cmd> <key> <flags> <exptime> <bytes>\r\n
 *     <data>\r\n
 * server sends:
 *     STORED\r\n
 *     NOT_STORED\r\n
 */
SEXP mc_store(SEXP mcon_s, SEXP key, SEXP value, SEXP exptime, SEXP cmd){
	int i;
	size_t true_vsize, protbufsize, true_lsize, cmd_size, start_cmd;
	char *outbuf, *response;
	mc_srv *srv;
	struct R_outpstream_st out;
	mc_con *mcon = unmarshall_con(mcon_s);

	if (!mcon) return ScalarLogical(FALSE);

	/* Determine which server we'll be working with */
	i = hash_servers(mcon,key);
	if (i == -1) return ScalarLogical(FALSE);
	srv = mcon->servers[i];

	/* Connect to it */
	if (srv->scon == -1 && (srv->scon = mc_SockConnect(srv->port,srv->host)) == -1)
		return ScalarLogical(FALSE);

	/* Allocate mem for serialized value plus protocol*/
	if ((mcon->obuf = init_store_buf(CHAR(STRING_ELT(cmd,0)),CHAR(STRING_ELT(key,0)),4096,INTEGER(exptime)[0],0)) == NULL)
		return ScalarLogical(FALSE);

	protbufsize = mcon->obuf->count;

	/* Serialize to buf. may have to use setjmp/longjmp if an error occurs.  */
	R_InitOutPStream(&out,mcon->obuf,R_pstream_xdr_format,0,
			outchar, outbytes, NULL, R_NilValue);
	R_Serialize(value,&out);
	true_vsize = mcon->obuf->count - protbufsize;

	/* Append "\r\n" */
	outbytes(&out,(void *)"\r\n",2);

	/* Grab size of serialized object and store in buf*/
	sprintf(
		(char *)((void *)mcon->obuf->buf + strlen(mcon->obuf->buf)),
		"%d\r\n",
		true_vsize
	);
	true_lsize = strlen(mcon->obuf->buf);

	start_cmd = protbufsize - true_lsize;

	/* Coallesce protocol line and serialized value */
	memcpy(mcon->obuf->buf+start_cmd,mcon->obuf->buf,true_lsize);

	cmd_size = mcon->obuf->count - start_cmd;

	/* Write full memcached command */
	seek_buf(mcon->obuf,start_cmd);
	if (cmd_size != writebytes_buf(srv,mcon->obuf,cmd_size)){
		destroy_iobufs(mcon);
		return ScalarLogical(FALSE);
	}

	/* Read result from server, should only be 1 line */
	if ((mcon->ibuf = init_buf(1)) == NULL){ 
		destroy_iobufs(mcon);
		return ScalarLogical(FALSE);
	}

	response = readline_buf(srv,mcon->ibuf);

	/* Network error or server sent an error message */
	if (response == NULL || error_occured(response)){
		destroy_iobufs(mcon);
		return ScalarLogical(FALSE);
	}

	/* Conditions for add or replace weren't met or
	 * key is in a delete queue
	 */
	if (strcmp("NOT_STORED\r",response) == 0){
		destroy_iobufs(mcon);
		return ScalarLogical(FALSE);
	}

	/* Success! */
	if (strcmp("STORED\r",response) == 0){
		destroy_iobufs(mcon);
		return ScalarLogical(TRUE);
	}

	/* shouldn't ever get here */
	destroy_iobufs(mcon);
	return ScalarLogical(FALSE);
}

static int inchar(R_inpstream_t stream){
	mc_buf *buf = stream->data;
	int c;
	if (buf->curpos >= buf->count)
		error("rmemcache: read error in inchar()");
	return buf->buf[buf->curpos++];
}

static void inbytes(R_inpstream_t stream, void *bytes, int length){
	mc_buf *buf = stream->data;
	if (buf->curpos + length >= buf->count)
		error("rmemcache: read error in inbytes()");

	memcpy(bytes,buf->buf + buf->curpos, length);
	buf->curpos += length;
}

SEXP mc_get(SEXP mcon_s, SEXP key_s){
	int i, keylen, flags;
	const char *key;
	char *response, *rptr;
	size_t bytes, startpos;
	mc_srv *srv;
	struct R_inpstream_st in;
	SEXP value;
	mc_con *mcon = unmarshall_con(mcon_s);

	if (!mcon) return R_NilValue;

	/* Determine which server we'll be working with */
	i = hash_servers(mcon,key_s);
	if (i == -1) return R_NilValue;
	srv = mcon->servers[i];

	/* Connect to it */
	if (srv->scon == -1 && (srv->scon = mc_SockConnect(srv->port,srv->host)) == -1)
		return R_NilValue;

	/* Send get command */
	key = CHAR(STRING_ELT(key_s,0));
	if ((mcon->obuf = init_get_buf(key)) == NULL)
		return R_NilValue;
	if (writeline_buf(srv,mcon->obuf) <= 0)
		return R_NilValue;

	/* Read result from server, should only be 1 line */
	if ((mcon->ibuf = init_buf(1)) == NULL) 
		return R_NilValue;

	response = readline_buf(srv,mcon->ibuf); 

	/* Network error or server sent an error message */
	if (response == NULL || error_occured(response))
		return R_NilValue;

	/* END reached ? */
	if (strncmp("END\r",response,4) == 0) /* Key not found. */
		return R_NilValue;

	/* VALUE */
	if (strncmp("VALUE",response,5) != 0) return R_NilValue;
	response += 6; /* move past "VALUE " */

	/* Save start position of unserialized variable */
	startpos = mcon->ibuf->curpos;

	/* Key */
	keylen = strlen(key);
	if (strncmp(response,key,keylen) != 0) return R_NilValue;
	response += keylen;

	/* Flags */
	rptr = NULL;
	flags = (int)strtol(response,&rptr,10);
	if (flags == 0 && response == rptr) /* error occured */
		return R_NilValue;

	if (rptr == NULL) return R_NilValue;

	response = rptr; 
					 

	/* bytes */
	rptr = NULL;
	bytes = (size_t)strtol(response,&rptr,10);
	if (flags == 0 && response == rptr) /* error occured */
		return R_NilValue;

	if (rptr == NULL) return R_NilValue;

	response = rptr;

	/* End of Line */
	if (strncmp("\r",response,1) != 0) return R_NilValue;

	/* Success! let's read result. */
	resize_buf(mcon->ibuf,bytes);
	if (!readbytes_buf(srv,mcon->ibuf,bytes))
		return ScalarLogical(FALSE);
	seek_buf(mcon->ibuf,startpos);

	/* Serialize to buf. may have to use setjmp/longjmp if buf error occurs*/
	R_InitInPStream(&in,mcon->ibuf,R_pstream_xdr_format,
			inchar, inbytes, NULL, R_NilValue);


	value =  R_Unserialize(&in);
	destroy_iobufs(mcon);
	return value;
}

SEXP mc_delete(SEXP mcon, SEXP key){
}

SEXP mc_incr(SEXP mcon, SEXP key, SEXP byval){
}

SEXP mc_decr(SEXP mcon, SEXP key, SEXP byval){
}

SEXP mc_stats(SEXP mcon, SEXP args){
}

SEXP mc_flushall(SEXP mcon, SEXP exptime){
}

SEXP mc_version(SEXP mcon){
}

SEXP mc_disconnect(SEXP mcon_s){
	mc_con *mcon = unmarshall_con(mcon_s);
	if (!mcon) return ScalarLogical(FALSE);

	return ScalarLogical(close_sockets(mcon));
}

#define CALLDEF(name, n)  { #name, (DL_FUNC) &name, n }

R_CallMethodDef callMethods[] = 
{
	CALLDEF(mc_connect,2),
	CALLDEF(mc_setservers,2),
	CALLDEF(mc_hashfun,2),
	CALLDEF(mc_hash,2),
	CALLDEF(mc_store,5),
	CALLDEF(mc_get,2),
	CALLDEF(mc_delete,2),
	CALLDEF(mc_incr,3),
	CALLDEF(mc_decr,3),
	CALLDEF(mc_stats,2),
	CALLDEF(mc_flushall,2),
	CALLDEF(mc_version,1),
	CALLDEF(mc_disconnect,1),
	CALLDEF(mc_destroy_iobufs,1),
	CALLDEF(mc_print_con,1),
	{NULL,NULL, 0}
};

void R_init_rmemcache(DllInfo *dll)
{
    MCCON_type_tag = install("MCCON_type_tag");
	R_registerRoutines(dll,NULL,callMethods,NULL,NULL);
}

void R_unload_rmemcache(DllInfo *dll)
{
}
