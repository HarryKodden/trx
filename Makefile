.PHONY: all configure compile build lint test run serve docker-images clean

DOCKER_DEV?=trx-development
DOCKER_RUNTIME?=trx-runtime
BUILD_TYPE?=RelWithDebInfo
SOURCE?=examples

DOCKER_DEV_SHELL:=docker run --rm -v "$(PWD)":/workspace --entrypoint bash $(DOCKER_DEV)

all: build

docker-dev:
	docker build --target builder -t $(DOCKER_DEV) .

docker-runtime:
	docker build -t $(DOCKER_RUNTIME) .

configure: docker-dev
	$(DOCKER_DEV_SHELL) -lc 'cmake -S /workspace -B /workspace/build -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)'

compile:
	$(DOCKER_DEV_SHELL) -lc 'cmake --build /workspace/build'

build: configure compile

lint: docker-dev
	$(DOCKER_DEV_SHELL) -lc 'cmake -S /workspace -B /workspace/build/lint -G Ninja -DCMAKE_BUILD_TYPE=$(BUILD_TYPE) -DCMAKE_CXX_FLAGS="-Wall -Wextra -Werror"'
	$(DOCKER_DEV_SHELL) -lc 'cmake --build /workspace/build/lint'

test: docker-dev
	$(DOCKER_DEV_SHELL) -lc 'cd /workspace/build && ctest --output-on-failure'

run: docker-runtime
	docker run --rm -v "$(PWD)":/workspace $(DOCKER_RUNTIME) /workspace/$(SOURCE)

serve: docker-runtime
	docker rm -f trx-server || true
	docker run --rm --name trx-server -v "$(PWD)":/workspace -p 8080:8080 $(DOCKER_RUNTIME) serve /workspace/$(SOURCE) &

debug-examples: docker-runtime
	docker run --rm -v "$(PWD)":/workspace $(DOCKER_RUNTIME) ls -l /workspace/examples

clean:
	rm -rf build
