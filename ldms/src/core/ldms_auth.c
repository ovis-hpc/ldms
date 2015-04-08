
/* get configured options */
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <string.h>
#include <time.h>
#include <errno.h>

#include <pwd.h>
#include <syslog.h>
#ifdef HAVE_AUTH
#include <openssl/evp.h>
#endif /* HAVE_AUTH */

#define NAME_LEN 512
#define CONFNAME ".ldmsauth.conf"
#define SYSCONFNAME "ldmsauth.conf"

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
	uint64_t ret = (uint64_t) hi;
	ret = (ret << 32);
	ret = ret | (uint64_t) lo;
	return ret;
}

uint64_t ldms_get_challenge()
{
#define SBUFSIZE 256
	struct random_data rbuf;
	int c0 = 0, c1 = 0;
	unsigned int seed;
	struct timespec t;
	uint64_t r = 0;
	char statebuf[SBUFSIZE];
	memset(&rbuf, 0, sizeof(rbuf));
	memset(statebuf, 0, sizeof(statebuf));
	clock_gettime(CLOCK_REALTIME, &t);
	seed = (unsigned int)t.tv_nsec;

	initstate_r(seed, &(statebuf[0]), sizeof(statebuf), &rbuf);
	random_r(&rbuf, &c0);
	random_r(&rbuf, &c1);
	r = ((uint64_t) c0) << 32;
	r ^= c1;
	return r;

}

#ifdef HAVE_AUTH

/** 
  Get secret from candidate file. Could be better: should enforce
  that candidate is not world readable but doesn't.
  \param filepath full path name of file to try reading secretword=X.
	filepath must be absolute and at least 3 characters or it
	will yield file not found result.
  \param secretword array size NAME_LEN+1 to store word if found.
  \return 0 if found with secret set, 1 if not found, 2 if denied,
	3 if file data isn't valid for secret.  
   For nonzero returns, secret word will be the empty string.
*/
static int try_password_file(const char *filepath, char *secretword,
				struct ldms_xprt *x) {
	secretword[0] = '\0';
	if (!filepath || strlen(filepath)<3 || filepath[0] != '/')
		return 1;

	errno = 0;
	int holderr;
	/* a minor file system race between access check and use */
	if ( (holderr = access(filepath, R_OK) ) != 0) {
		switch (holderr) {
		case EACCES:
			x->log(LDMS_LERROR,
				"Cannot read secret word from file %s\n",
				filepath);
			return 2;
		case ENOENT:
			x->log(LDMS_LDEBUG,
				"File of secret word not found: %s\n",
				filepath);
			return 1;
		default:
			x->log(LDMS_LDEBUG,
				"Bad secret word filename: %s\n",
				filepath);
			return 1;
		}
	}
	struct stat st;
	if (stat(filepath, &st)) {
		holderr = errno;
		x->log(LDMS_LERROR,"%s: %s while trying to stat %s\n",
			__FILE__, strerror(errno), filepath);
		return 2;
	}
	if ((st.st_uid == getuid()) && (st.st_mode & 077) != 0) {
		x->log(LDMS_LERROR,"@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");
			x->log(LDMS_LERROR,"@     WARNING: UNPROTECTED SECRET WORD FILE!     @\n");
		x->log(LDMS_LERROR,"Permissions 0%3.3o for '%s' are too open.",
			(u_int)st.st_mode & 0777, filepath);
		x->log(LDMS_LERROR,"It your secret word files must NOT accessible by others.");
		x->log(LDMS_LERROR,"This secret will be ignored.");
		return 2;
	}
	FILE *conf_file = NULL;
	conf_file = fopen(filepath, "r");

	errno = 0;
	if (NULL == conf_file) {
		holderr = errno;
		x->log(LDMS_LERROR,"%s: %s while trying to open %s\n",
			__FILE__, strerror(errno), filepath);
		return 2;
	}
	char input_line[NAME_LEN + 1];
	while (fgets(input_line, NAME_LEN, conf_file) != NULL) {
		if (input_line[0] == '#' || input_line[0] == '\0')
			continue;
		if (strncmp(input_line, "secretword=", 11) == 0) {
			strncpy(secretword, &input_line[11], NAME_LEN);
			secretword[NAME_LEN] = '\0'; /* just being cautious */
#ifdef HAVE_AUTHDEBUG
			x->log(LDMS_LINFO,"found secretword= in: %s\n",
				 filepath);
			break;
#endif
		}
	}
	fclose(conf_file);
	if (strlen(secretword) < 6) {
		secretword[0] = '\0';
		x->log(LDMS_LERROR, "Invalid data in secret word file %s\n",
			filepath);
		x->log(LDMS_LERROR,
			 "Need secretword=X where X longer than 4 letters\n");
		return 3;
	}
	return 0;
}

char *ldms_get_auth_string(uint64_t n, ldms_t x_)
{

	struct passwd *pwent;
	char  secretword[NAME_LEN + 1];
	char *ldmsauth_path = NULL;
	char *result = NULL;
	struct ldms_xprt *x = (struct ldms_xprt *)x_;
#ifdef HAVE_AUTHDEBUG
	x->log(LDMS_LINFO,"%s:%d: %s\n", __FUNCTION__, __LINE__, __FILE__);
#endif

	errno = 0;
	if ((pwent = getpwuid(getuid())) == NULL) {	/* for real id */
		x->log(LDMS_LERROR,"%s:%d: %s\n", __FILE__, __LINE__, strerror(errno));
		return NULL;
	}

	/*
	 * We look for a readable .conf in the following order.
	 * - LDMS_AUTH_FILE set in environment
	 * - .ldmsauth.conf in the user's home directory
	 * - The system wide default in SYSCONFDIR/ldmsauth.conf
	 * which should have 600 perm
	 */
	ldmsauth_path = getenv("LDMS_AUTH_FILE");
	int fc = try_password_file(ldmsauth_path,secretword,x);
	if (fc) {
		/* By far, the largest we'll need */
		size_t ldmsauth_path_len = strlen(pwent->pw_dir)
		    + strlen(SYSCONFDIR) + strlen(CONFNAME) + 2;

		ldmsauth_path = alloca(sizeof(char) * ldmsauth_path_len);
		/* try ~/.ldmsauth.conf */
		snprintf(ldmsauth_path, ldmsauth_path_len - 1, "%s/" CONFNAME,
			 pwent->pw_dir);
		fc = try_password_file(ldmsauth_path, secretword, x);
		if (fc) {
			/* try /etc/ldmsauth.conf */
			snprintf(ldmsauth_path, ldmsauth_path_len - 1, 
				"%s/" SYSCONFNAME, SYSCONFDIR);
			fc = try_password_file(ldmsauth_path, secretword, x);
		}
	
	}
	if (fc) {
		x->log(LDMS_LERROR,"%s: unable to find a usable secret in"
			"%s/%s or %s/%s or from LDMS_AUTH_FILE shell "
			"variable.\n",
			__FILE__,
			 pwent->pw_dir,CONFNAME,
			 SYSCONFDIR,SYSCONFNAME);
		return NULL;
	}

	size_t len = strlen(secretword) + 2 + strlen(xstr(UINT64_MAX));
	result = malloc(len);
	if (!result) {
		x->log(LDMS_LERROR, "%s auth key malloc failed.\n",
			__FILE__);
		return result;
	}
	snprintf(result, len, "%" PRIu64 "%s", n, secretword);

	EVP_MD_CTX *mdctx;
	const EVP_MD *md;
	unsigned char md_value[EVP_MAX_MD_SIZE];
	unsigned int md_len = 0, i;

	md = EVP_sha224();

	mdctx = EVP_MD_CTX_create();
	EVP_DigestInit_ex(mdctx, md, NULL);
	EVP_DigestUpdate(mdctx, result, strlen(result));
	EVP_DigestFinal_ex(mdctx, md_value, &md_len);
	EVP_MD_CTX_destroy(mdctx);
	free(result);
	result = malloc(2 * EVP_MAX_MD_SIZE + 1);
	if (!result) {
		x->log(LDMS_LERROR, "%s auth key malloc failed.\n",
			__FILE__);
		return result;
	}
	for (i = 0; i < md_len; i++) {
		snprintf(&result[2 * i], 3, "%02x", md_value[i]);
	}
#ifdef HAVE_AUTHDEBUG
	x->log(LDMS_LINFO, "secret key is: %s\n", result);
#endif

	return result;
}

#endif /* HAVE_AUTH */
