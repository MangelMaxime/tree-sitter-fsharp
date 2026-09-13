#!/bin/sh
set -e
cd "$(dirname "$0")"
dotnet run --project build/EasyBuild.fsproj -- "$@"
