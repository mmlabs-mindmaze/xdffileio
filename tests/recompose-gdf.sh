#!/bin/sh

set -ex

# gen broken.gdf, broken.gdf.code, broken.gdf.event
$BUILDDIR/tests/gen-broken-gdf

# merge those 3 files into fixed.gdf
$BUILDDIR/src/gdf-repair broken.gdf fixed.gdf

# test fixed.gdf contains all the expected data
$BUILDDIR/tests/check-fixed-gdf

# remove generated files
rm broken.gdf broken.gdf.code broken.gdf.event
