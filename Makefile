.PHONY: all configure compile build lint test examples run serve docker-images clean load-test load-test-quick load-test-medium load-test-heavy demo-monitoring

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

test-postgres: docker-dev
	docker compose up -d postgres
	@echo "Waiting for PostgreSQL to be ready..."
	@sleep 2
	docker run --rm -v "$(PWD)":/workspace --network trx_default \
		-e TEST_DB_BACKENDS=postgresql \
		-e POSTGRES_HOST=postgres \
		-e POSTGRES_PORT=5432 \
		-e POSTGRES_DB=trx \
		-e POSTGRES_USER=trx \
		-e POSTGRES_PASSWORD=password \
		--entrypoint bash $(DOCKER_DEV) -lc 'cd /workspace/build && ctest --output-on-failure'
	docker compose down

test-odbc: docker-dev
	docker compose up -d postgres
	@echo "Waiting for PostgreSQL to be ready..."
	@sleep 2
	docker run --rm -v "$(PWD)":/workspace --network trx_default \
		-e TEST_DB_BACKENDS=odbc \
		-e ODBC_CONNECTION_STRING="DRIVER={PostgreSQL Unicode};SERVER=postgres;PORT=5432;DATABASE=trx;UID=trx;PWD=password;" \
		--entrypoint bash $(DOCKER_DEV) -lc 'cd /workspace/build && ctest --output-on-failure'
	docker compose down

test-all: docker-dev
	docker compose up -d postgres
	@echo "Waiting for PostgreSQL to be ready..."
	@sleep 2
	docker run --rm -v "$(PWD)":/workspace --network trx_default \
		-e TEST_DB_BACKENDS=all \
		-e POSTGRES_HOST=postgres \
		-e POSTGRES_PORT=5432 \
		-e POSTGRES_DB=trx \
		-e POSTGRES_USER=trx \
		-e POSTGRES_PASSWORD=password \
		-e ODBC_CONNECTION_STRING="DRIVER={PostgreSQL Unicode};SERVER=postgres;PORT=5432;DATABASE=trx;UID=trx;PWD=password;" \
		--entrypoint bash $(DOCKER_DEV) -lc 'cd /workspace/build && ctest --output-on-failure'
	docker compose down

examples: docker-dev
	$(DOCKER_DEV_SHELL) -lc 'cd /workspace && for file in examples/*.trx; do echo -n "$$file: "; ./build/src/trx "$$file" >/dev/null 2>&1 && echo "OK" || echo "FAILED"; done'

run: build
	$(DOCKER_DEV_SHELL) -lc './build/src/trx examples/sample.trx'

serve: docker-runtime
	docker rm -f trx-server || true
	docker run --rm --name trx-server -v "$(PWD)":/workspace -p 8080:8080 $(DOCKER_RUNTIME) serve /workspace/$(SOURCE) &

debug-examples: docker-runtime
	$(DOCKER_DEV_SHELL) ls -l /workspace/examples

# Load testing targets
load-test-quick:
	@echo "Quick load test: 100 requests, 10 concurrent users"
	./tools/load_test.sh -n 100 -c 10

load-test-medium:
	@echo "Medium load test: 1000 requests, 20 concurrent users"
	./tools/load_test.sh -n 1000 -c 20

load-test-heavy:
	@echo "Heavy load test: 10000 requests, 50 concurrent users"
	./tools/load_test.sh -n 10000 -c 50

load-test:
	@echo "Custom load test - use arguments like: make load-test ARGS='-n 5000 -c 30'"
	@if [ -z "$(ARGS)" ]; then \
		echo "No ARGS specified, running default: -n 1000 -c 20"; \
		./tools/load_test.sh -n 1000 -c 20; \
	else \
		./tools/load_test.sh $(ARGS); \
	fi

demo-monitoring:
	@echo "Starting load testing + monitoring demo..."
	./tools/demo-monitoring.sh

release:
	@echo "Creating a new release..."
	./tools/create-release.sh

clean:
	rm -rf build
	rm -rf tools/venv
