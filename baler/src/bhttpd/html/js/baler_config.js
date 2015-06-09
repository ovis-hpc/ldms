/*
 * This file shall contains only global configuration variables for baler.
 */
window.bcfg = {
	/* Put configurations here. */
	bhttpd: {
		/* URI to bhttpd that corresponds to master balerd store */
		master_uri: "http://"+window.location.hostname+":18888",

		/* URIs to other bhttpd in the case that we have multiple
		 * stores managed my multiple balerd. This list can be empty. */
		other_uris: [
		]
	}
};
