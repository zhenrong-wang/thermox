FROM ubuntu:24.04 AS native-builder

ARG DEBIAN_FRONTEND=noninteractive
ARG THERMOX_ENABLE_CANTERA=ON

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libboost-dev \
        libcurl4-openssl-dev \
        libpq-dev \
        libssl-dev \
        libsuitesparse-dev \
        pkg-config \
        python3 \
        python3-packaging \
        scons \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /cantera-source
COPY modules/properties/cantera/ ./

RUN mkdir -p /opt/cantera \
    && if [ "${THERMOX_ENABLE_CANTERA}" = "ON" ]; then \
        scons -j1 build \
            prefix=/opt/cantera \
            clib_legacy=y \
            python_package=n \
        && scons -j1 install \
            prefix=/opt/cantera \
            clib_legacy=y \
            python_package=n; \
    fi

RUN mkdir -p \
        /opt/cantera-runtime/lib \
        /opt/cantera-runtime/share/cantera \
    && if [ "${THERMOX_ENABLE_CANTERA}" = "ON" ]; then \
        cp -a /opt/cantera/lib/libcantera_shared.so* \
            /opt/cantera-runtime/lib/ \
        && cp -a /opt/cantera/share/cantera/data \
            /opt/cantera-runtime/share/cantera/; \
    fi

ENV PKG_CONFIG_PATH=/opt/cantera/lib/pkgconfig

WORKDIR /src
COPY . .

RUN --mount=type=cache,id=thermox-cmake-build,target=/build \
    cmake -S . -B /build \
        -DCMAKE_BUILD_TYPE=Release \
        -DTHERMOX_ENABLE_CANTERA=${THERMOX_ENABLE_CANTERA} \
    && cmake --build /build \
        --target thermox_api_server thermox_worker \
        --parallel 1 \
    && mkdir -p /out \
    && cp /build/adapters/http/thermox_api_server /out/ \
    && cp /build/adapters/host/thermox_worker /out/

FROM ubuntu:24.04 AS thermox-runtime

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        libcurl4t64 \
        liblapack3 \
        libpq5 \
        libssl3t64 \
        libumfpack6 \
    && rm -rf /var/lib/apt/lists/* \
    && useradd --system --uid 10001 --create-home thermox

COPY --from=native-builder /out/thermox_api_server /usr/local/bin/
COPY --from=native-builder /out/thermox_worker /usr/local/bin/
COPY --from=native-builder /opt/cantera-runtime /opt/cantera

ENV CANTERA_DATA=/opt/cantera/share/cantera/data
ENV LD_LIBRARY_PATH=/opt/cantera/lib
ENV OMP_NUM_THREADS=1
ENV OPENBLAS_NUM_THREADS=1
ENV MKL_NUM_THREADS=1
ENV VECLIB_MAXIMUM_THREADS=1
ENV NUMEXPR_NUM_THREADS=1

USER thermox
WORKDIR /home/thermox

CMD ["thermox_api_server", "--listen", "0.0.0.0", "--port", "8080", "--allow-insecure-remote"]
