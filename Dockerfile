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
    && make compat \
    && make dashboard-build

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

CMD ["python3", "-m", "bridge_service"]
