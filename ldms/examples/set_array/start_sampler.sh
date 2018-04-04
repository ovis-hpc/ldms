#!/bin/bash

# Run the sampler daemon in the foreground
ldmsd -F -x sock:10001 -c set_array_sampler.cfg -v INFO
