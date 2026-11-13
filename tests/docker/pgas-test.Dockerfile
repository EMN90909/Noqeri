FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends libssl3t64 ca-certificates && rm -rf /var/lib/apt/lists/*
COPY pgas_remote_node /usr/local/bin/pgas_remote_node
COPY pgas_remote_client /usr/local/bin/pgas_remote_client
