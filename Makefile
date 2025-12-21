.PHONY: all configure compile build test run docker-images clean

DOCKER_DEV?=trx-rewrite-dev
DOCKER_RUNTIME?=trx-rewrite
BUILD_TYPE?=RelWithDebInfo
SOURCE?=examples/sample.trx

DOCKER_DEV_SHELL:=docker run --rm -v "$(PWD)":/workspace --entrypoint bash $(DOCKER_DEV)

all: build

configure:
	$(DOCKER_DEV_SHELL) -lc 'cmake -S /workspace -B /workspace/build -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)'

compile:
	$(DOCKER_DEV_SHELL) -lc 'cmake --build /workspace/build'

build: configure compile

test:
	$(DOCKER_DEV_SHELL) -lc 'cd /workspace/build && ctest --output-on-failure'

run:
	docker run --rm -v "$(PWD)":/workspace $(DOCKER_RUNTIME) /workspace/$(SOURCE)

docker-images:
	docker build -t $(DOCKER_RUNTIME) .
	docker build --target builder -t $(DOCKER_DEV) .

clean:
	rm -rf build
