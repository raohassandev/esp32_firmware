#pragma once
#include <stddef.h>

/* The pre-built, pre-compressed bundles. See tools/build_bundle.py. */
const char *web_assets_bundle_js(size_t *length);
const char *web_assets_bundle_js_gz(size_t *length);
const char *web_assets_bundle_css(size_t *length);
const char *web_assets_bundle_css_gz(size_t *length);

const char *web_assets_index(size_t *length);
