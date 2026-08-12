# Conformal Canvas

Conformal Canvas 是一个基于 C++23 的图像变换服务，用于把输入图片转换成复变函数映射后的结果。

项目支持两类处理：
- `Escher` 风格图像变换
- 自定义复变函数的 conformal 变换

服务默认监听 `7854` 端口，同时支持 IPv4 和 IPv6。

## 本地安装依赖
```sh
sudo apt install -y \
	libboost-system-dev \
	libboost-url-dev \
	libopencv-dev \
	libmuparserx-dev
```

## 从源码构建
```sh
git clone --recursive https://github.com/quqiOnfree/conformal_canvas.git
cd conformal_canvas
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

## 运行服务
```sh
./build/conformal_canvas
```

## HTTP 接口
### 0. 使用说明
- 方法：`GET`
- 路径：`/`
- 响应：`text/plain` 的接口使用说明（端点、参数、示例）

示例：
```sh
curl http://127.0.0.1:7854/
```

### 通用查询参数
- `format`：输出格式，`png`（默认）或 `gif`
  - `format=gif` 时对输入图片逐帧变换并输出动画 GIF（帧延迟 100ms）
  - 输入为 GIF 而 `format=png` 时仅处理第一帧

### 1. Escher 变换
- 方法：`POST`
- 路径：`/handle_escher_image`
- 请求体：原始图片二进制
- Content-Type：`image/png`、`image/jpeg` 等 `image/*`
- 响应：处理后的 `PNG` 图片

示例：
```sh
curl -X POST \
	-H "Content-Type: image/png" \
	--data-binary @input.png \
	http://127.0.0.1:7854/handle_escher_image \
	--output output.png
```

### 2. Conformal 变换
- 方法：`POST`
- 路径：`/handle_conformal_image?func=...`
- 请求体：原始图片二进制
- `func`：复变函数表达式，变量名为 `z`，默认 `log(z)`
- 响应：处理后的 `PNG` 图片

示例：
```sh
curl -X POST \
	-H "Content-Type: image/png" \
	--data-binary @input.png \
	"http://127.0.0.1:7854/handle_conformal_image?func=exp%28z%29" \
	--output output.png
```
其中 `'(' == %28` , `')' == %29`, `exp(z) == exp%28z%29`

### 3. GIF 动画变换
对动画 GIF 的每一帧做变换，输出仍为动画 GIF：
```sh
curl -X POST \
	-H "Content-Type: image/gif" \
	--data-binary @input.gif \
	"http://127.0.0.1:7854/handle_escher_image?format=gif" \
	--output output.gif
```

### 常见返回
- `404 Not Found`：路径不正确
- `400 Bad Request`：不是 `POST`、缺少参数或图片为空
- `415 Unsupported Media Type`：`Content-Type` 不是 `image/*`

## 预编译二进制
推送 `v*` 标签时 GitHub Actions 自动构建并发布 Release：
- `conformal_canvas-linux-x86_64.tar.gz`
- `conformal_canvas-windows-x64.zip`（含 exe 与所需 DLL）

也可在 Actions 页面手动触发 `Release Binaries` 工作流，构建产物以 artifact 形式保留。

## Docker
仓库会自动发布镜像到 GHCR，镜像名为：
```sh
ghcr.io/quqionfree/conformal_canvas
```

拉取最新镜像：
```sh
docker pull ghcr.io/quqionfree/conformal_canvas:latest
```

运行容器：
```sh
docker run -d -p 7854:7854 ghcr.io/quqionfree/conformal_canvas:latest
```

然后就可以像本地运行一样访问 HTTP 接口。

## 自行构建 Docker 镜像
```sh
docker build -t conformal_canvas:local .
docker run -d -p 7854:7854 conformal_canvas:local
```
