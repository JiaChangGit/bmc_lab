#!/usr/bin/env bash
set -euo pipefail

# 安裝本專案在 Ubuntu apt 環境建置、執行 Demo 與跑單元測試需要的套件。
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  cmake \
  ninja-build \
  pkg-config \
  curl \
  jq \
  dbus-user-session \
  libboost-all-dev \
  nlohmann-json3-dev \
  libspdlog-dev \
  libsystemd-dev \
  libgtest-dev

echo "Dependencies installed for Ubuntu apt environment."
