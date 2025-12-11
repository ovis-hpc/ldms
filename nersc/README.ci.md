# Image based slurm contatiner image


## Update the FROM in nersc/Dockerfile.ci

```
FROM registry.nersc.gov/csg/slurm_build_cos_3.1:20250221 as ldms_build
```

## Build and Tag

```
podman build . -f nersc/Dockerfile.ci --tag ldms_build_cos_3.1:20250221 --build-arg VER="0.1.0"
TMPDIR=~/tmp/ podman build . -f nersc/Dockerfile.ci --tag ldms_build_cos_3.1:20250221 --build-arg VER="0.1.0"
podman tag localhost/ldms_build_cos_3.1:20250221 registry.nersc.gov/csg/ldms_build_cos_3.1:20250221
podman image list |grep ldms_build_cos
```

## Update image in nersc/start_build_container.bash 

```
vi nersc/start_build_container.bash
```

## Run test build

```
export LDMS_REPO=$(pwd)
export NERSC_ZYPPER_REPO=~/software/git/nersc-zypper
nersc/start_build_container.bash
alvarez-mgr:/ # /builds/nersc/csg/ovis/ && ./nersc/test_build.bash
```

## Push to registry

```
podman push registry.nersc.gov/csg/ldms_build_cos_3.1:20250221
podman search --list-tags --limit 999 registry.nersc.gov/csg/ldms_build_cos_3.1
```

## Update image in ci/cd

```
vi .gitlab-ci.yml
```
