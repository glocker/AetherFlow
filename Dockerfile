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
    PORT=8080 \
    AETHERFLOW_LOG_DIR=/app/logs

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=build /app /app

EXPOSE 8080

CMD ["./tools/run_hosted_demo.sh"]
