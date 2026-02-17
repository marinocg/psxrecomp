FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential cmake ninja-build g++-13 \
        libsdl2-dev python3 \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-13 CXX=g++-13

WORKDIR /psxrecomp
COPY . .

# Build psxrecomp (clean build directory first to avoid stale cache)
RUN rm -rf build && cmake -S . -B build -G Ninja \
        -DBUILD_TESTS=ON -DBUILD_CLI=ON \
    && cmake --build build

# Run unit tests
RUN ctest --test-dir build --output-on-failure

ENTRYPOINT ["bash", "/psxrecomp/docker_test_demos.sh"]
