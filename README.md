# piecewise
Piecewise debloating toolchain

### How to use
+ Install [Docker](https://docs.docker.com/install/linux/docker-ce/ubuntu/#install-docker-ce)
+ Load piece-wise docker image  
`docker load --input piece-wise.docker`
+ Run piece-wise image  
`docker run -it --cap-add=SYS_PTRACE --security-opt seccomp=unconfined piecewise0001bloat/piecewise`
+ Read README files inside docker image about how to use the toolchain
