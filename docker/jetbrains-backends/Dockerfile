# Air-gapped JetBrains artifact server for the CLion Dev Container.
#
# This image is the *serving* half of the fully-offline CLion setup described in
# https://www.jetbrains.com/help/clion/fully-offline-mode.html#config_local_jb_client
#
FROM espressif/idf:release-v6.1
USER root

# ---------------------------------------------------------------------------
# nginx (the web server for step 6 below).
#
# `--no-install-recommends` keeps the image small; ca-certificates/curl are
# retained for debugging the mirror from inside the container.
# ---------------------------------------------------------------------------
RUN apt-get update \
    && DEBIAN_FRONTEND=noninteractive apt-get upgrade -y \
    && DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        nginx \
        ca-certificates \
        curl \
    && rm -rf /var/lib/apt/lists/* \
    && rm -f /etc/nginx/sites-enabled/default \
    && nginx -v


# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# See https://www.jetbrains.com/help/clion/fully-offline-mode.html
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------

RUN mkdir /JetBrains
WORKDIR /JetBrains

# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------
# Set up the air-gapped environment
# ---------------------------------------------------------------------------
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# Step 1. Download and unpack Jetbrains Client Downloader
# ---------------------------------------------------------------------------
RUN \
    curl \
        --output jetbrains-clients-downloader.tar.gz \
        https://download-cdn.jetbrains.com/idea/code-with-me/backend/jetbrains-clients-downloader-linux-x86_64-2149.tar.gz && \
    tar zxvf jetbrains-clients-downloader.tar.gz && \
    rm jetbrains-clients-downloader.tar.gz  && \
    ln -s \
        /JetBrains/jetbrains-clients-downloader-linux-x86_64-2149  \
        jetbrains-clients-downloader-linux-x86_64

# ---------------------------------------------------------------------------
# Step 2, execute jetbrains-clients-downloader with --download-backends
# ---------------------------------------------------------------------------
RUN  \
    /bin/bash \
        /JetBrains/jetbrains-clients-downloader-linux-x86_64/bin/jetbrains-clients-downloader \
        --products-filter CL \
        --build-filter 262.10968.117 \
        --include-eap-builds \
        --platforms-filter linux-x64 \
        --download-backends /JetBrains/Desktop

# ---------------------------------------------------------------------------
# Step 3, execute jetbrains-clients-downloader without --download-backends
# ---------------------------------------------------------------------------
RUN  \
    /bin/bash \
        /JetBrains/jetbrains-clients-downloader-linux-x86_64/bin/jetbrains-clients-downloader \
        --verbose \
        --products-filter CL \
        --build-filter 262.10968.117 \
        --include-eap-builds \
        --platforms-filter linux-x64 \
        /JetBrains/Desktop

# ---------------------------------------------------------------------------
# Make the files visible to nginx
# ---------------------------------------------------------------------------
RUN chmod --recursive +r /JetBrains

# ---------------------------------------------------------------------------
# Hand nginx the mirror's server block 
#
# Ubuntu's stock nginx.conf ends with `include /etc/nginx/conf.d/*.conf;`, so
# dropping the file there is enough for nginx to pick it up; nginx.conf itself
# needs no edits. The stock `default` site on port 80 was removed in the nginx
# install step above, leaving this the only server block.
# ---------------------------------------------------------------------------
COPY nginx.conf /etc/nginx/conf.d/jetbrains-backends.conf
RUN nginx -t

# nginx listens on 8080 (unprivileged) so the port mapping and the CLion/Gateway
# URLs are identical to the previous python http.server setup.
EXPOSE 8080

CMD ["nginx", "-g", "daemon off;"]
