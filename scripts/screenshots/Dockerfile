FROM funiq-trading-mock-dev:latest
RUN sed -i 's|http://ports.ubuntu.com|https://ports.ubuntu.com|g' /etc/apt/sources.list \
    && apt-get -o Acquire::Retries=1 -o Acquire::https::Timeout=15 update \
    && apt-get install -y --no-install-recommends xvfb xterm scrot openbox fonts-dejavu-core \
    && rm -rf /var/lib/apt/lists/*
