# CSOS — Single native binary with LLVM JIT

# STAGE 1: Build
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential llvm-18 llvm-18-dev \
    libcurl4-openssl-dev curl ca-certificates \
    && rm -rf /var/lib/apt/lists/*

ENV LLVM_PREFIX=/usr/lib/llvm-18
ENV PATH="/usr/lib/llvm-18/bin:$PATH"

WORKDIR /build
COPY lib/ lib/
COPY src/ src/
COPY specs/ specs/
COPY Makefile .

RUN make clean && make all && ./csos --test 2>&1 | tail -1

# STAGE 2: Runtime
FROM ubuntu:24.04

RUN apt-get update && apt-get install -y --no-install-recommends \
    curl ca-certificates libcurl4 \
    && rm -rf /var/lib/apt/lists/*

COPY --from=builder /build/csos /usr/local/bin/csos
RUN chmod +x /usr/local/bin/csos

WORKDIR /app
COPY specs/ specs/
COPY .canvas-tui/ .canvas-tui/
COPY .opencode/ .opencode/
COPY scripts/ scripts/
COPY AGENTS.md README.md ./

RUN mkdir -p .csos/rings .csos/sessions
# Initialize ecosystem rings via native binary
RUN echo '{"action":"diagnose"}' | /usr/local/bin/csos | tail -1

HEALTHCHECK --interval=30s --timeout=10s --retries=3 \
    CMD curl -sf http://localhost:4200/api/state || exit 1

EXPOSE 4200

CMD ["/usr/local/bin/csos", "--http", "4200"]
