"""Custom Bazel repository helpers for HCOM."""

def _parse_cmake_version(cmake_content):
    major = "0"
    minor = "0"
    patch = "1"
    for raw_line in cmake_content.splitlines():
        line = raw_line.strip()
        if line.startswith("set(LIB_VERSION_MAJOR"):
            tokens = [t for t in line.replace(")", "").split(" ") if t]
            if len(tokens) >= 2:
                major = tokens[1]
        elif line.startswith("set(LIB_VERSION_MINOR"):
            tokens = [t for t in line.replace(")", "").split(" ") if t]
            if len(tokens) >= 2:
                minor = tokens[1]
        elif line.startswith("set(LIB_VERSION_PATCH"):
            tokens = [t for t in line.replace(")", "").split(" ") if t]
            if len(tokens) >= 2:
                patch = tokens[1]
    return major, minor, patch

def _escape_string(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')

def _hcom_build_metadata_impl(ctx):
    cmake_path = ctx.path(ctx.attr.cmake_file)
    cmake_content = ctx.read(cmake_path)
    major, minor, patch = _parse_cmake_version(cmake_content)

    # Align default behavior with CMake/build.sh.
    component_version = "1.0.0"
    commit_id = "<unknown>"

    workspace_root = str(ctx.path(ctx.attr.workspace_file).dirname)
    git_result = ctx.execute(["git", "-C", workspace_root, "rev-parse", "HEAD"], quiet = True)
    if git_result.return_code == 0:
        commit_id = git_result.stdout.strip()

    defs_bzl = """
HCOM_COMPONENT_VERSION = \"{component}\"
HCOM_COMMIT_ID = \"{commit}\"
HCOM_LIB_VERSION = \"{lib_version}\"
HCOM_SOVERSION = \"{soversion}\"
""".format(
        component = _escape_string(component_version),
        commit = _escape_string(commit_id),
        lib_version = _escape_string("{}.{}.{}".format(major, minor, patch)),
        soversion = _escape_string(major),
    )

    ctx.file("defs.bzl", defs_bzl)
    ctx.file("BUILD.bazel", "exports_files([\"defs.bzl\"])\n")
    ctx.file("WORKSPACE.bazel", "workspace(name = \"{}\")\n".format(ctx.name))

hcom_build_metadata_repository = repository_rule(
    implementation = _hcom_build_metadata_impl,
    attrs = {
        "cmake_file": attr.label(mandatory = True, allow_single_file = True),
        "workspace_file": attr.label(default = Label("//:WORKSPACE"), allow_single_file = True),
    },
)

def _has_urma_headers(ctx, include_prefix):
    return ctx.path(include_prefix + "/urma_types.h").exists and ctx.path(include_prefix + "/urma_ubagg.h").exists

def _prepare_urma_from_system(ctx):
    if _has_urma_headers(ctx, "/usr/include/ub/umdk/urma") or _has_urma_headers(ctx, "/usr/include/umdk/urma"):
        ctx.symlink("/usr/include", "include")
        return True
    return False

def _prepare_urma_from_repo(ctx):
    clone_result = ctx.execute([
        "git",
        "clone",
        "--depth",
        "1",
        "--branch",
        ctx.attr.umdk_branch,
        ctx.attr.umdk_remote,
        "umdk_src",
    ], quiet = True)
    if clone_result.return_code != 0:
        fail("failed to clone umdk from {} (branch={}): {}".format(
            ctx.attr.umdk_remote,
            ctx.attr.umdk_branch,
            clone_result.stderr,
        ))

    copy_script = """
set -e
mkdir -p include/ub/umdk/urma include/umdk/urma include/ub/umdk include/umdk
cp -f umdk_src/src/urma/lib/urma/bond/include/*.h include/ub/umdk/urma/ 2>/dev/null || true
cp -f umdk_src/src/urma/lib/urma/core/include/*.h include/ub/umdk/urma/ 2>/dev/null || true
cp -f umdk_src/src/urma/lib/uvs/core/include/*.h include/ub/umdk/urma/ 2>/dev/null || true
cp -f include/ub/umdk/urma/*.h include/umdk/urma/ 2>/dev/null || true
"""
    copy_result = ctx.execute(["bash", "-c", copy_script], quiet = True)
    if copy_result.return_code != 0:
        fail("failed to prepare urma headers from umdk: {}".format(copy_result.stderr))

    if not (_has_urma_headers(ctx, "include/ub/umdk/urma") or _has_urma_headers(ctx, "include/umdk/urma")):
        fail("urma headers are missing after preparing fallback repository")

def _hcom_urma_impl(ctx):
    ctx.symlink(ctx.path(ctx.attr.build_file), "BUILD.bazel")
    ctx.file("WORKSPACE.bazel", "workspace(name = \"{}\")\n".format(ctx.name))

    if _prepare_urma_from_system(ctx):
        return

    _prepare_urma_from_repo(ctx)

hcom_urma_repository = repository_rule(
    implementation = _hcom_urma_impl,
    attrs = {
        "build_file": attr.label(mandatory = True, allow_single_file = True),
        "umdk_remote": attr.string(default = "https://atomgit.com/openeuler/umdk.git"),
        "umdk_branch": attr.string(default = "br_openEuler_24.03_LTS_SP3"),
    },
)