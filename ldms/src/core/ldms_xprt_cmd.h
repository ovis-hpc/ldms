/* This file must NOT have an ifdef include guard added.
 It is intended to appear multiple times.
*/
/** Xmacros definition for our command list, so that any change to
  command list is automatically propagated everywhere.

  Changes to this command list break the binary network interface;
  clients and servers must be changed out simultaneously.

Each use of this include file will look something like:

	some C preamble code
	#define X(a) some_expansion_based_on_a
	#include "ldms_xprt_cmd.h"
	#undef X
	some C finish code
For examples:
enum cmd {
#define X(a) a,
#include "ldms_xprt_cmd.h"
#undef X
};

extern const char *names[];

const char *names[] = [
#define X(a) #a,
#include "ldms_xprt_cmd.h"
#undef X
];

// This function will collapse to a constant.
int sizeof_names() {
  int result=0;
#define X(a) result++;
#include "ldms_xprt_cmd.h"
#undef X
  return result;
}

*/
X(LDMS_CMD_DIR)
X(LDMS_CMD_DIR_CANCEL)
X(LDMS_CMD_LOOKUP)
X(LDMS_CMD_UPDATE)
X(LDMS_CMD_REQ_NOTIFY)
X(LDMS_CMD_CANCEL_NOTIFY)
X(LDMS_CMD_AUTH)
X(LDMS_CMD_AUTH_PASSWORD)
/* All reply types must follow LDMS_CMD_REPLY */
X(LDMS_CMD_REPLY)
X(LDMS_CMD_DIR_REPLY)
X(LDMS_CMD_LOOKUP_REPLY)
X(LDMS_CMD_REQ_NOTIFY_REPLY)
X(LDMS_CMD_AUTH_REPLY)

/* Transport private requests set bit 32.
with LDMS_CMD_XPRT_PRIVATE; it is not a valid command.
This has the desirable side effect of forcing the compilers to
allocate at least 4 bytes for the enum which can then move as
a network int.
*/

