# Release packaging image: the Zalos source baked on top of a prebuilt toolchain base image.
#
# The three release bases (Dockerfile, Dockerfile.ubuntu, Dockerfile.nix) hold only the toolchain and are built once
# by the base-images workflow. This layer adds the checkout with a single COPY — no RUN step, so nothing executes at
# build time and building the linux/arm64 variant costs nothing but a layer (no emulation, no toolchain install).
#
# Why the source is baked in instead of bind-mounted: a bind mount only works when the engine sees the same
# filesystem as the job. A CI runner whose docker daemon is remote or containerized mounts an empty /workspace, and
# the packaging script then dies with "bash: /workspace/packaging/.../build-*.sh: No such file or directory"
# (exit 127). A build context and `docker cp` travel through the docker API, so they work on any runner.
# scripts/ci-package.sh builds this image, runs the packaging script in it and copies dist/ back out.
#
# No `# syntax=` directive on purpose: nothing here needs BuildKit-only features and the frontend image must not be
# re-resolved for the cross-architecture builds. .dockerignore applies (so no .git/, build trees or artifacts);
# packaging/rpm/build-rpm.sh therefore packs its source tarball from an exclude list, not from `git ls-files`.
ARG BASE_IMAGE=localhost/zalos-fedora:43
FROM ${BASE_IMAGE}

COPY . /workspace/
WORKDIR /workspace
