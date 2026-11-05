FROM ubuntu:24.04
RUN apt-get update && apt-get install -y --no-install-recommends g++ make ca-certificates && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY . .
RUN make feedback_server && mkdir -p data
ENV PORT=8080
EXPOSE 8080
CMD ["sh","-c","./feedback_server --demo --host 0.0.0.0 --port ${PORT}"]
