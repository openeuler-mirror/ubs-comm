# C API for general things

[TOC]

## 1 Version

### 1.1 Get version of library

#### Functionality description

Get version of this library, e.g. 0.1.0.
Format: {major_version}.{minor_version}.{fix}

#### Function definition

```c
const char *ubsocket_version()
```

#### Description of parameters and return value

| Parameters/return | In/Out | Description                   |
|-------------------|--------|-------------------------------|
| return            |        | string of version, e.g. 0.1.0 |

## 2 Initialization and un-initialization

### 2.1 Initialization

#### Functionality description

Set the u_init_options_t to default.

#### Function definition

```c
int ubsocket_init_options(u_init_options_t *options);
```

#### Description of parameters and return value

| Parameters/return | In/Out | Description                   |
|-------------------|--------|-------------------------------|
| options           | in     | options to be initialized     |
| return            |        | 0 if successful, <0 if failed |

