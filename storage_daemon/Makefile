
all: clean daemon test

test:
	go build testApp.go
daemon:
	go build sdaemon.go
clean:
	rm -f testApp sdaemon
