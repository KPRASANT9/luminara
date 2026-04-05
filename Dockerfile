# CSOS Production Stack with LLVM JIT

# STAGE 1: Build
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential llvm-18 llvm-18-dev libprotobuf-dev protobuf-compiler \
    libcurl4-openssl-dev curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV LLVM_PREFIX=/usr/lib/llvm-18
ENV PATH="/usr/lib/llvm-18/bin:$PATH"

WORKDIR /build

COPY lib/ lib/
COPY src/ src/
COPY Makefile .

RUN make clean && make all

RUN ls -la csos

# STAGE 2: Runtime
FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    python3 python3-pip python3-venv curl ca-certificates \
    libprotobuf32 libcurl4 \
    libnss3 libnspr4 libatk1.0-0t64 libatk-bridge2.0-0t64 libcups2t64 libdrm2 \
    libdbus-1-3 libxkbcommon0 libatspi2.0-0t64 libxcomposite1 libxdamage1 \
    libxfixes3 libxrandr2 libgbm1 libpango-1.0-0 libcairo2 libasound2t64 \
    && rm -rf /var/lib/apt/lists/*

RUN curl -fsSL https://deb.nodesource.com/setup_20.x | bash - \
    && apt-get install -y nodejs \
    && rm -rf /var/lib/apt/lists/*

RUN npm i -g opencode-ai@latest

RUN pip3 install --break-system-packages requests playwright \
    && playwright install chromium

COPY --from=builder /build/csos /usr/local/bin/csos

RUN chmod +x /usr/local/bin/csos

WORKDIR /app

COPY lib/ lib/
COPY src/ src/
COPY .opencode/ .opencode/
COPY AGENTS.md README.md pyproject.toml ./
COPY scripts/ scripts/

RUN mkdir -p .csos/rings .csos/sessions

RUN python3 -c 'import sys; sys.path.insert(0,"."); from src.core.core import Core; c=Core(); [c.grow(r) for r in ["eco_domain","eco_cockpit","eco_organism"] if r not in c.rings_list()]; print("Initialized",len(c.rings_list()),"rings")'

HEALTHCHECK --interval=30s --timeout=10s --retries=3 \
    CMD /usr/local/bin/csos --test 2>&1 | grep -q "TESTS PASSED" || exit 1

RUN chmod +x scripts/watchdog.sh

EXPOSE 4096 4097 4098

CMD ["/bin/bash", "-c", "/usr/local/bin/csos --http 4097 & opencode serve --port 4096"]
