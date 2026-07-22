FROM debian:bookworm-slim AS build

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        make \
        nodejs \
        npm \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY openmct/package.json openmct/package-lock.json ./openmct/
RUN npm ci --prefix openmct

COPY . .
RUN make clean \
    && make test \
    && make backend \
    && make compat \
    && make dashboard-build

FROM debian:bookworm-slim AS runtime

ENV DEBIAN_FRONTEND=noninteractive \
    AETHERFLOW_BRIDGE_URL=http://127.0.0.1:8080 \
    AETHERFLOW_DASHBOARD_HOST=0.0.0.0 \
    AETHERFLOW_DASHBOARD_URL=http://127.0.0.1:5173 \
    AETHERFLOW_LOG_DIR=/app/logs

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        nodejs \
        npm \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app /app

EXPOSE 8080 5173

CMD ["./tools/run_demo.sh"]
