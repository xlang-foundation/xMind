# Use Ubuntu 22.04 as the base image
FROM ubuntu:22.04

# Set non-interactive mode for apt-get
ENV DEBIAN_FRONTEND=noninteractive

# Update package lists and install prerequisites
RUN apt-get update && apt-get install -y \
    software-properties-common \
    build-essential \
    wget \
    curl \
    git \
    ca-certificates \
    cmake \
    libssl-dev \
    zlib1g-dev \
    libbz2-dev \
    libreadline-dev \
    libsqlite3-dev \
    libncurses5-dev \
    libgdbm-dev \
    libnss3-dev \
    libffi-dev \
    liblzma-dev \
    tk-dev \
    uuid-dev \
    xz-utils

# Add the deadsnakes PPA to install Python 3.12
RUN add-apt-repository ppa:deadsnakes/ppa -y && \
    apt-get update && \
    apt-get install -y python3.12 python3.12-dev python3.12-venv python3-pip

# Verify OpenSSL version (should be >= 3.0)
RUN openssl version

# Install GCC 13 and G++ 13 from the Ubuntu Toolchain PPA
RUN add-apt-repository ppa:ubuntu-toolchain-r/test -y && \
    apt-get update && \
    apt-get install -y gcc-13 g++-13 && \
    update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# Set Python 3.12 as the default python3 and ensure pip is installed
RUN update-alternatives --install /usr/bin/python3 python3 /usr/bin/python3.12 1 && \
    python3.12 -m ensurepip --upgrade && \
    python3.12 -m pip install --upgrade pip

# Install NumPy using pip
RUN python3.12 -m pip install numpy

# Set up Python environment variables for CMake to find Python 3.12
ENV PYTHON_EXECUTABLE=/usr/bin/python3.12
ENV PYTHON_INCLUDE_DIR=/usr/include/python3.12
ENV PYTHON_LIBRARY=/usr/lib/x86_64-linux-gnu/libpython3.12.so

# Clone the xMind repository
RUN git clone -b shawn-framework https://github.com/xlang-foundation/xMind.git

# Clone xlang into xMind/ThirdParty
RUN git clone -b Jit https://github.com/xlang-foundation/xlang.git xMind/ThirdParty/xlang

# Set working directory to xMind
WORKDIR /xMind

# Build the project
RUN mkdir build && cd build && cmake .. && make -j$(nproc)

# Copy the install script to build/bin, make it executable, and run it
WORKDIR /xMind/build/bin
RUN cp /xMind/ThirdParty/xlang/PyEng/install_py_xlang.sh . && \
    chmod +x install_py_xlang.sh && \
    ./install_py_xlang.sh

# Set entrypoint (optional)
CMD ["/bin/bash"]
