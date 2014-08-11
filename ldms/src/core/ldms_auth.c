

/* get configured options */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <pwd.h>
#include <syslog.h>
#include <openssl/evp.h>

#define NAME_LEN 512
#define CONFNAME ".ldmsauth.conf"

#include "ldms.h"
#include "ldms_xprt.h"

#define xstr(x) str(x)
#define str(x) #x

#ifdef NEEDS_SNPRINTF_DECL
int snprintf(char *str, size_t size, const char *format, ...);
#endif

uint64_t ldms_unpack_challenge(uint32_t chi, uint32_t clo)
{
	uint32_t hi = ntohl(chi);
	uint32_t lo = ntohl(clo);
	int64_t ret = (int64_t) hi;
	ret = (ret << 32);
	ret = ret | (int64_t) lo;
	return ret;
}

uint64_t ldms_get_challenge()
{
#define SBUFSIZE 256
	struct random_data rbuf;
	int c0=0, c1=0;
	unsigned int seed;
	struct timespec t;
	uint64_t r = 0;
	char statebuf[SBUFSIZE];
	memset(&rbuf,0,sizeof(rbuf));
	memset(statebuf,0,sizeof(statebuf));
	clock_gettime(CLOCK_REALTIME, &t);
	seed = (unsigned int)t.tv_nsec;

	initstate_r(seed, &(statebuf[0]), sizeof(statebuf), &rbuf);
	random_r(&rbuf, &c0);
	random_r(&rbuf, &c1);
	r = ((uint64_t)c0) <<32;
	r ^= c1;
	return r;

}

char *ldms_get_auth_string(uint64_t n, ldms_t x_)
{

    struct passwd *pwent;
    char input_line[NAME_LEN+1], secretword[NAME_LEN+1];
    FILE *conf_file=NULL;
    char *ldmsauth_path = NULL;
    char *result = NULL;
    int holderr = 0;
	struct ldms_xprt *x = (struct ldms_xprt *)x_;
#ifdef HAVE_AUTHDEBUG
	x->log("%s:%d: %s\n",__FUNCTION__,__LINE__,__FILE__);
#endif

    errno = 0;
    if ((pwent = getpwuid(getuid())) == NULL)    /* for real id */
    {
		x->log("%s:%d: %s\n",__FILE__,__LINE__,strerror(errno));
        return NULL;
    }

    /*
	 * We look for a readable .conf in the following order.
     * - LDMS_AUTH_FILE set in environment
	 * - .conf in the user's home directory
	 * - The system wide default in SYSCONFDIR/ldmsauth.conf
	 * which should have 600 perm
     */
    ldmsauth_path = getenv("LDMS_AUTH_FILE");
    if ( ! (ldmsauth_path && access( ldmsauth_path, R_OK ) == 0) ){
        /* By far, the largest we'll need */
        size_t ldmsauth_path_len = strlen(pwent->pw_dir) \
            + strlen(SYSCONFDIR) + strlen(CONFNAME) +2;

        ldmsauth_path = (char*) malloc( sizeof(char) * ldmsauth_path_len );
        if ( ! ldmsauth_path ){
            /* ENOMEM */
			x->log("%s:%d: %s\n",__FILE__,__LINE__,"out of memory");
            goto err;
        }
        snprintf( ldmsauth_path, ldmsauth_path_len-1, "%s/" CONFNAME , pwent->pw_dir );
        if ( access( ldmsauth_path, R_OK ) != 0 )
            snprintf( ldmsauth_path, ldmsauth_path_len-1, "%s/" CONFNAME, SYSCONFDIR );
    }
	errno=0;
    conf_file = fopen( ldmsauth_path, "r");

    if (conf_file == NULL)
    {
	    holderr = errno;
		x->log("%s:%d: %s while trying to open %s\n",__FILE__,__LINE__,strerror(errno),ldmsauth_path);
        goto err;
    }
    secretword[0] = '\0';
    while (fgets(input_line,NAME_LEN,conf_file) != NULL)
    {
        input_line[strlen(input_line)-1] = '\0';  /* eliminate \n */
        if (input_line[0] == '#'  ||  input_line[0] == '\0')
            continue;
	if (strncmp(input_line,"secretword=",11) == 0 ) {
	    strncpy(secretword,&input_line[11],NAME_LEN);
	    secretword[NAME_LEN] = '\0';  /* just being cautious */
#ifdef HAVE_AUTHDEBUG
			x->log("secret word is: %s\n",secretword);
#endif
	}
    }
    if (secretword[0] == '\0')
    {
		x->log("%s:%d: %s\n",__FILE__,__LINE__,
			"Did not find secretword in ldmsauth conf file");
	holderr = ENOMSG;
        goto err;
    }

    size_t len = strlen(secretword) + 2 + strlen( xstr( UINT64_MAX ));
    result = malloc(len);
	snprintf(result, len, "%" PRIu64 "%s", n, secretword);

	EVP_MD_CTX *mdctx;
	const EVP_MD *md;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len=0, i;

	md = EVP_sha224();

	mdctx = EVP_MD_CTX_create();
	EVP_DigestInit_ex(mdctx, md, NULL);
	EVP_DigestUpdate(mdctx, result, strlen(result));
	EVP_DigestFinal_ex(mdctx, md_value, &md_len);
	EVP_MD_CTX_destroy(mdctx);
	free(result);
	result = malloc(2*EVP_MAX_MD_SIZE+1);
	for(i = 0; i < md_len; i++) {
		snprintf(&result[2*i],3,"%02x", md_value[i]);
	}
#ifdef HAVE_AUTHDEBUG
	x->log("secret key is: %s\n",result);
#endif
 
err:
    free(ldmsauth_path);
    if (conf_file) {
        fclose(conf_file);
    }
    errno = holderr;
    return result;
}

