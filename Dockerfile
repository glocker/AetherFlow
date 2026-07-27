FROM debian:bookworm-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        nodejs \
        npm \
        python3 \
        python3-pytest \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY openmct/package.json openmct/package-lock.json ./openmct/
RUN npm ci --prefix openmct

COPY . .
RUN python3 -m compileall aetherflow bridge_service eps_emulator compat/python tests/python \
    && python3 -m pytest \
    && python3 compat/python/check_vectors.py compat/vectors/aetherflow_can_vectors.json \
    && npm --prefix openmct run build

FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        iproute2 \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app /app

EXPOSE 8080

CMD ["python3", "-m", "aetherflow"]
