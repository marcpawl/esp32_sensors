#!/bin/bash -ex
cd $(dirname $0)
uv run src/deepseek_alert/__init__.py
