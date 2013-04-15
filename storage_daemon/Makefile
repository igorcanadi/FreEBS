
all: clean daemon test

test: testApp.go
	go build testApp.go
daemon: sdaemon.go
	go build sdaemon.go

run:
	./sdaemon > slog.txt &	# Server
	./testApp > clog.txt	# Client

clean:
	rm -f testApp sdaemon *.txt
