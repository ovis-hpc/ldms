#!/bin/bash

# Run the sampler daemon in the foreground
ldmsd -F -x sock:10000 -c set_array_agg.cfg -v INFO
