FROM emscripten/emsdk:latest

WORKDIR /src

# Copy only what the simulator needs (keeps image context fast)
COPY components /src/components
COPY main /src/main
COPY sim /src/sim

RUN emcmake cmake -S /src/sim -B /build -DCMAKE_BUILD_TYPE=Release && \
    cmake --build /build -j"$(nproc)" && \
    mkdir -p /srv && \
    cp /build/m5stopwatch_sim.html /srv/index.html && \
    cp /build/m5stopwatch_sim.js /srv/m5stopwatch_sim.js && \
    cp /build/m5stopwatch_sim.wasm /srv/m5stopwatch_sim.wasm

EXPOSE 8080

CMD ["python3", "-m", "http.server", "8080", "--bind", "0.0.0.0", "--directory", "/srv"]
