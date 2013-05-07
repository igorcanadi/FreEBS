#!/usr/bin/env python

import argparse, socket, getpass, paramiko, time, threading, sys

class Piper(threading.Thread):
    def __init__(self, inp, out, host):
        self.inp = inp
        self.out = out
        self.host = host
        super(Piper, self).__init__(target=self)
        self.daemon = True

    def __call__(self, *args, **kwargs):
        for line in self.inp:
            self.out.write(str(self.host) + ': ' + line)

class Host(object):
    hosts = list()

    def connect(self, username, password):
        # first do the SSHClient
        self.client = paramiko.SSHClient()
        self.client.load_system_host_keys()
        self.client.set_missing_host_key_policy(paramiko.WarningPolicy())
        self.client.connect(self.hostname, 22, username, password)
        # next do the SFTPClient
        self.transport = paramiko.Transport((self.hostname, 22))
        self.transport.connect(username=username, password=password)
        self.sftp = paramiko.SFTPClient.from_transport(self.transport)
        Host.hosts.append(self)

    def do(self, cmd):
        print self.hostname + ': ' + cmd
        (_, out, err) = self.client.exec_command(cmd)
        Piper(out, sys.stdout, self).start()
        Piper(err, sys.stderr, self).start()

    def copy(self, local, remote):
        rv = self.sftp.put(local, remote)
        self.sftp.chmod(remote, 0700)

    def __init__(self, hostname, username, password):
        self.hostname = hostname
        self.ip = socket.gethostbyname(hostname)
        self.connect(username, password)

    def __del__(self):
        self.client.close()
        self.transport.close()

    def __str__(self):
        return self.hostname

def get_pass(username):
    return getpass.getpass('Password for %s@cs: ' % username)

def main():
    parser = argparse.ArgumentParser(
            description='Launch the driver module and replica managers.',
            epilog='%(prog)s uses ssh to launch everything.')
    parser.add_argument('--username', nargs=1, type=str,
            default=getpass.getuser(),
            help='username to use when sshing into best-mumble (default %s)' % getpass.getuser())
    parser.add_argument('--replicas', nargs='+', type=str, required=True, 
            help='the ordered list of replica managers')
    parser.add_argument('--client', nargs=1, type=str, required=True, 
            help='the hostname where the driver should be launched')
    parser.add_argument('--module', nargs=1, type=str,
            default='./kernel/freebs.ko',
            help='local filename of kernel module')
    parser.add_argument('--rmgr', nargs=1, type=str,
            default='~/bin/sdaemon',
            help='remote filename of storage daemon')
    args = parser.parse_args()

    username = args.username
    password = get_pass(username)
    paramiko.util.log_to_file('paramiko.log')

    replicas = map(lambda replica: Host(replica, username, password), args.replicas)
    client = Host(args.client[0], 'igor', '!')

    print 'installing sdaemon...'
    for i in range(len(replicas)):
        replica = replicas[i]
        #replica.copy(args.rmgr, '/tmp/sdaemon')
        replica.do('rm -f /tmp/vdisk')
        if i == 0:
            replica.do(args.rmgr + ' -c /tmp/vdisk -n ' + replicas[i + 1].hostname)
        elif i == len(replicas) - 2:
            replica.do(args.rmgr + ' -c /tmp/vdisk -p ' + replicas[i - 1].hostname + 
                    ' -n ' + replicas[i + 1].hostname)
        else:
            replica.do(args.rmgr + ' -c /tmp/vdisk -p ' + replicas[i - 1].hostname)

    client.copy(args.module, '/tmp/freebs.ko')
    client.do('sudo insmod /tmp/freebs.ko replicas=' + ','.join([i.ip for i in replicas]))

    try:
        while True:
            time.sleep(10)
    except KeyboardInterrupt, e:
        pass

if __name__ == '__main__':
    main()
