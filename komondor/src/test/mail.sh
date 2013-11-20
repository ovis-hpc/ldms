#!/bin/bash

echo "($KMD_MODEL_ID, $KMD_COMP_ID, $KMD_SEVERITY_LEVEL) $KMD_SEC.$KMD_USEC mail $1" >> /home/narate/projects/OVIS/komondor/src/test/mail.log
