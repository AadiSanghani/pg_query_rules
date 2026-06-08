FROM --platform=linux/arm64 postgres:18

RUN apt-get update && apt-get install -y \
    build-essential \
    postgresql-server-dev-18 \
    vim \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /extension
