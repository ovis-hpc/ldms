/* compile with gcc -o mad_query new_mad_query.c -O3 -g -I/usr/include/infiniband -libmad -libumad -losmcomp -libnetdisc -pthread ; # on mlx5 systems */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <umad.h>
#include <mad.h>
#include <iba/ib_types.h>
#include <errno.h>
#include <stdbool.h>
#include <linux/types.h>
#include <endian.h>
struct ibmad_port *srcport;

int mgmt_classes[3] = { IB_SMI_CLASS, IB_SA_CLASS, IB_PERFORMANCE_CLASS };
ib_portid_t portid = { 0 };
int mask = 0xffff;
uint64_t ext_mask = 0xffffffffffffffffULL;
uint16_t cap_mask;
int ibd_ca_port = 1;
char *ibd_ca = NULL;
static char c[64]="mlx5_0";
static uint8_t rcv_buf[1024];
void *p;
uint64_t val64;
int port=1; /*allow the sampler to change this later */
int dec_val;
int espeed;


#if 0
struct mad_port {
	char *ca_name;       /* Name of the device      */
	int portnum;	 /* Physical port number    */
	uint base_lid;       /* LMC of LID	      */
	uint sm_lid;	 /* SM LID		  */
	uint sm_sl;	  /* SM service level	*/
	uint state;	  /* Logical port state      */
	uint phys_state;     /* Physical port state     */
	uint rate;	   /* Port link bit rate      */
	uint64_t capmask;    /* Port capabilities       */
	uint64_t gid_prefix; /* Gid prefix of this port */
	uint64_t port_guid;  /* GUID of this port       */
};
#endif

umad_ca_t hca;



#define GSI_CONT 0
#define NOMASK 0
#define NOMASK2 0
// the low and high for a contiguous group of counter ids */
struct counter_range {
/* to manage config, add a name and enabled flag to each counter range */
	unsigned enabled; // 0 not, 1 enabled, 2 continuation of prior w/same GSI id.
	const char *subset;
	int lo;
	int hi;
	int id; // query gsi id, or GSI_CONT if continuation of prior query.
	uint16_t mask_check; // cap_mask needed for valid set
	uint32_t mask_check2; // cap_mask2 needed for valid set
	int32_t qidx_start;
	struct mad_query_set *qs;
};

/* Keep this array 64 elements or less. The true/false flags are combined
 * into a 64 bit hash code and may be appended as a hex string to the schema name.
 *
 * We need to review and set better enable 0/1 defaults.
 */
struct counter_range cr_default[] = {
	{ 0, "base", IB_PC_ERR_SYM_F, IB_PC_ERR_RCVCONSTR_F, IB_GSI_PORT_COUNTERS, NOMASK, NOMASK2, 0 , 0 },
	{ 2, "base2", IB_PC_ERR_LOCALINTEG_F, IB_PC_XMT_WAIT_F, GSI_CONT, NOMASK, NOMASK2, 0 },

	{ 1, "extended", IB_PC_EXT_XMT_BYTES_F, IB_PC_EXT_RCV_PKTS_F, IB_GSI_PORT_COUNTERS_EXT, NOMASK, NOMASK2, 0 , 0 },
	{ 2, "extended2", IB_PC_EXT_XMT_UPKTS_F, IB_PC_EXT_RCV_MPKTS_F, GSI_CONT, IB_PM_EXT_WIDTH_SUPPORTED, NOMASK2, 0 , 0 },
	{ 2, "extended3", IB_PC_EXT_ERR_SYM_F, IB_PC_EXT_QP1_DROP_F, GSI_CONT, NOMASK, NOMASK2, 0 , 0 },

	{ 1, "xmtsl", IB_PC_XMT_DATA_SL0_F, IB_PC_XMT_DATA_SL15_F, IB_GSI_PORT_XMIT_DATA_SL, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "rcvsl", IB_PC_RCV_DATA_SL0_F, IB_PC_RCV_DATA_SL15_F, IB_GSI_PORT_RCV_DATA_SL, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "xmtdisc", IB_PC_XMT_INACT_DISC_F, IB_PC_XMT_SW_HOL_DISC_F, IB_GSI_PORT_XMIT_DISCARD_DETAILS, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "rcverr", IB_PC_RCV_LOCAL_PHY_ERR_F, IB_PC_RCV_LOOPING_ERR_F, IB_GSI_PORT_RCV_ERROR_DETAILS, NOMASK, NOMASK2, 0 , 0 },
	{ 0, "oprcvcounters", IB_PC_PORT_OP_RCV_PKTS_F, IB_PC_PORT_OP_RCV_DATA_F, IB_GSI_PORT_PORT_OP_RCV_COUNTERS, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "flowctlcounters", IB_PC_PORT_XMIT_FLOW_PKTS_F, IB_PC_PORT_RCV_FLOW_PKTS_F, IB_GSI_PORT_PORT_FLOW_CTL_COUNTERS, NOMASK, NOMASK2, 0 , 0 },
	{ 0, "vloppackets", IB_PC_PORT_VL_OP_PACKETS0_F, IB_PC_PORT_VL_OP_PACKETS15_F, IB_GSI_PORT_PORT_VL_OP_PACKETS, NOMASK, NOMASK2, 0 , 0 },
	{ 0, "vlopdata", IB_PC_PORT_VL_OP_DATA0_F, IB_PC_PORT_VL_OP_DATA15_F, IB_GSI_PORT_PORT_VL_OP_DATA, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "vlxmitflowctlerrors", IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS0_F, IB_PC_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS15_F, IB_GSI_PORT_PORT_VL_XMIT_FLOW_CTL_UPDATE_ERRORS, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "vlxmitcounters", IB_PC_PORT_VL_XMIT_WAIT0_F, IB_PC_PORT_VL_XMIT_WAIT15_F, IB_GSI_PORT_PORT_VL_XMIT_WAIT_COUNTERS, NOMASK, NOMASK2, 0 , 0 },
	{ 0, "swportvlcong", IB_PC_SW_PORT_VL_CONGESTION0_F, IB_PC_SW_PORT_VL_CONGESTION15_F, IB_GSI_SW_PORT_VL_CONGESTION, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "rcvcc", IB_PC_RCV_CON_CTRL_PKT_RCV_FECN_F, IB_PC_RCV_CON_CTRL_PKT_RCV_BECN_F, IB_GSI_PORT_RCV_CON_CTRL, NOMASK, NOMASK2, 0 , 0 },
	{ 0, "slrcvfecn", IB_PC_SL_RCV_FECN0_F, IB_PC_SL_RCV_FECN15_F, IB_GSI_PORT_SL_RCV_FECN, NOMASK, NOMASK2, 0 , 0 },
	{ 0, "slrcvbecn", IB_PC_SL_RCV_BECN0_F, IB_PC_SL_RCV_BECN15_F, IB_GSI_PORT_SL_RCV_BECN, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "xmitcc", IB_PC_XMIT_CON_CTRL_TIME_CONG_F, IB_PC_XMIT_CON_CTRL_TIME_CONG_F, IB_GSI_PORT_XMIT_CON_CTRL, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "vlxmittimecc", IB_PC_VL_XMIT_TIME_CONG0_F, IB_PC_VL_XMIT_TIME_CONG14_F, IB_GSI_PORT_VL_XMIT_TIME_CONG, NOMASK, NOMASK2, 0 , 0 },
	{ 1, "smplctl", IB_PSC_OPCODE_F, IB_PSC_SAMPLES_ONLY_OPT_MASK_F, IB_GSI_PORT_SAMPLES_CONTROL, NOMASK, NOMASK2, 0 , 0 }
	// not supporting extended_speeds_query
};

#define ARRAY_SIZE(array) (sizeof(array) / sizeof(*array))
#define NGROUPS ARRAY_SIZE(cr_default)

struct query_result {
        const char *field;
        int dec_val;
        uint64_t val64;
};

struct mad_query_set {
        size_t n;
        struct query_result q[];
};

// ignoring enabled flag
int mad_query_set_create(struct counter_range *r, size_t len_r)
{
        struct mad_query_set *qs;
        size_t n = 0;
        int i = 0;
        // count slots
        for (i ; i < len_r; i++) {
		printf("subset %d %s\n", i, r[i].subset);
                n = r[i].hi - r[i].lo + 1;
		// alloc flexarray struct
		size_t bytes = sizeof(struct mad_query_set) + n * sizeof(struct query_result);
		r[i].qs = calloc(1, bytes);
		if (!r[i].qs) {
			errno = ENOMEM;
			return errno; 
		}
		r[i].qs->n = n;
		// populate dec_val and names in slots
		int j = 0, dec_val;
		for (dec_val = r[i].lo; dec_val <= r[i].hi; dec_val++) {
			r[i].qs->q[j].dec_val = dec_val;
			r[i].qs->q[j].field = mad_field_name(dec_val);
			printf("%d: %s\n", dec_val, r[i].qs->q[j].field ? r[i].qs->q[j].field : "UNDEFINED" );
			j++;
		}
	}
	return 0;
}

void mad_query_set_destroy(struct mad_query_set *qs)
{
	if (qs)
		free(qs);
}

int main(int argc,char **argv)
{

	portid.drpath.cnt = 0;
	memset(portid.drpath.p, 0, 63);
	portid.drpath.drslid = 0;
	portid.drpath.drdlid = 0;
	portid.grh_present = 0;
	memset(portid.gid, 0, 16);
	portid.qp = 1;
	portid.qkey = 0;
	portid.sl = 0;
	portid.pkey_idx = 0;

	/* portid.lid=217; */
	portid.lid = atoi(argv[1]);
	int rc = 0;

	int serr =  mad_query_set_create(cr_default, NGROUPS);
	if (serr) {
		printf("Failed to alloc query array %d\n", (int)NGROUPS);
		return 1;
	}

	ibd_ca = strdup(c);
	if (!ibd_ca) {
		printf("Failed to copy interface name %s\n", c);
		rc = 1;
		goto cleanup;
	}

	srcport = mad_rpc_open_port(ibd_ca, ibd_ca_port, mgmt_classes, 3);
	if (!srcport) {
		printf("Failed to open %s port %d\n", ibd_ca, ibd_ca_port);
		rc = 1;
		goto cleanup;
	}
	// get cap info
	uint8_t pc[1024];
	memset(pc, 0, sizeof(pc));
	if (!pma_query_via(pc, &portid, port, 0/*timeout*/, CLASS_PORT_INFO,
                           srcport)) {
		printf("failed get class port info\n");
		rc = 1;
		goto cleanup;
	}
        __be32 be_cap_mask2;
        uint32_t cap_mask2;
        __be16 cap_mask;
	memcpy(&cap_mask, pc + 2, sizeof(cap_mask));
	memcpy(&be_cap_mask2, pc + 4, sizeof(be_cap_mask2));
	cap_mask2 = ntohl(be_cap_mask2) >> 5;

	printf("cap_mask %x cap_mask2 %x\n", cap_mask, cap_mask2);
	size_t g;
	uint8_t *res = NULL;
	for (g = 0 ; g < NGROUPS; g++) {
		struct counter_range *cr = &cr_default[g];
		printf("read subset %zu %s\n", g, cr->subset);
		switch(cr->enabled) {
		case 0:
			res = NULL;
			goto cont;
		case 1:
			memset(rcv_buf, 0, sizeof(rcv_buf));
			res = pma_query_via(rcv_buf, &portid, port, 0, cr->id, srcport);
			if (!res) {
				printf ("perfquery error at %zu\n", g);
				rc = 1;
				goto cont;
			}
		case 2:
			break;
		}

		size_t k, klast = cr->hi - cr->lo + 1;
		if (res) {
			if (cr->mask_check && !(cr->mask_check & cap_mask)) {
				printf("%s not supported by cap_mask\n",cr->subset);
				goto cont;
			}
			if (cr->mask_check2 && !(cr->mask_check2 & htonl(cap_mask2))) {
				printf("%s not supported by cap_mask2 ck2 %x mask2 %x\n", cr->subset,
				cr->mask_check2, htonl(cap_mask2));
				goto cont;
			}
			for (k = 0 ; k < klast; k++) {
				struct query_result *q = &(cr->qs->q[k]);		
				mad_decode_field(rcv_buf, q->dec_val, &(q->val64));
				printf("%s: %" PRIu64 "\t %" PRIx64"\n", q->field, q->val64, q->val64);
			}
		} else {
			if ( cr->enabled == 1) {
				for (k = 0 ; k < klast; k++) {
					struct query_result *q = &(cr->qs->q[k]);		
					printf("%s: n/a\t n/a\n", q->field);
				}
			}
		}
cont:
		mad_query_set_destroy(cr->qs);
		cr->qs = NULL;
	}

cleanup:
	for (g = 0 ; g < NGROUPS; g++) {
		mad_query_set_destroy(cr_default[g].qs);
	}

	if (srcport)
		mad_rpc_close_port(srcport);

	if (ibd_ca)
		free(ibd_ca);

	return rc;
}
