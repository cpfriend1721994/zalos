ARG BASE_IMAGE=localhost/zalos-fedora:43
FROM ${BASE_IMAGE}

COPY . /workspace/
WORKDIR /workspace
