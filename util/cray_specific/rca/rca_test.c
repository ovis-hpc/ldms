#include <stdio.h>
#include <stdlib.h>
#include <rca_lib.h>
#include <rs_id.h>
#include <rs_meshcoord.h>

typedef struct {
        int x, y, z;
} nettopo_coord_t;

int main(){
	nettopo_coord_t nettopo_coord;
	rs_node_t node;
	mesh_coord_t loc;
	uint16_t nid;
	int rc;

	rc = rca_get_nodeid(&node);
	if (rc != 0){
	  printf("rca_get_nodeid failed!\n");
	}
	nid = (uint16_t)node.rs_node_s._node_id;
	rca_get_meshcoord(nid, &loc);
	if (rc != 0){
	  printf("rca_get_meshcoord failed!\n");
	}
	nettopo_coord.x = loc.mesh_x;
	nettopo_coord.y = loc.mesh_y;
	nettopo_coord.z = loc.mesh_z;

	printf("nid %d (%d %d %d)\n", nid, nettopo_coord.x, nettopo_coord.y, nettopo_coord.z);

	return 0;
}

