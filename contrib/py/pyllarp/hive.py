#!/usr/bin/env python3
import pyllarp
from time import sleep
from signal import signal, SIGINT
from shutil import rmtree
from os import makedirs
from socket import AF_INET, htons, inet_aton
from pprint import pprint
import sys

tmpdir = "/tmp/lokinet_hive"

def RemoveTmpDir(dirname):
  if dirname.startswith("/tmp/") and len(dirname) > 5:
    print("calling rmdir -r %s" % dirname)
    if (input("Is this ok? (y/n): ").lower().strip()[:1] == "y"):
      rmtree(dirname, ignore_errors=True)
  else:
    print("not removing dir %s because it doesn't start with /tmp/" % dirname)

endpointName = "pyllarp"

def MakeEndpoint(router, after):
  if router.IsRelay():
    return
  ep = pyllarp.Endpoint(endpointName, router)
  router.AddEndpoint(ep)
  if after is not None:
    router.CallSafe(lambda : after(ep))

def AddRouter(hive, index, netid="hive"):
  dirname = "%s/routers/%d" % (tmpdir, index)
  makedirs("%s/netdb" % dirname, exist_ok=True)

  config = pyllarp.Config()

  port = index + 30000
  tunname = "lokihive%d" % index

  config.router.encryptionKeyfile = "%s/encryption.key" % dirname
  config.router.transportKeyfile = "%s/transport.key" % dirname
  config.router.identKeyfile = "%s/identity.key" % dirname
  config.router.ourRcFile = "%s/rc.signed" % dirname
  config.router.netid = netid
  config.router.nickname = "Router%d" % index
  config.router.publicOverride = True
  config.router.overrideAddress("127.0.0.1", '{}'.format(port))
  """
  config.router.ip4addr.sin_family = AF_INET
  config.router.ip4addr.sin_port = htons(port)
  config.router.ip4addr.sin_addr.set("127.0.0.1")
  """
  config.router.blockBogons = False

  config.network.enableProfiling = False
  config.network.routerProfilesFile = "%s/profiles.dat" % dirname
  config.network.netConfig = {"type": "null"}

  config.netdb.nodedbDir = "%s/netdb" % dirname

  config.links.InboundLinks = [("lo", AF_INET, port, set())]

  config.system.pidfile = "%s/lokinet.pid" % dirname

  config.dns.netConfig = {"local-dns": ("127.3.2.1:%d" % port)}

  if index != 1:
    config.bootstrap.routers = ["%s/routers/1/rc.signed" % tmpdir]

  hive.AddRouter(config)


def AddClient(hive, index, netid="hive"):
  dirname = "%s/clients/%d" % (tmpdir, index)
  makedirs("%s/netdb" % dirname, exist_ok=True)

  config = pyllarp.Config()

  port = index + 40000
  tunname = "lokihive%d" % index

  config.router.encryptionKeyfile = "%s/encryption.key" % dirname
  config.router.transportKeyfile = "%s/transport.key" % dirname
  config.router.identKeyfile = "%s/identity.key" % dirname
  config.router.ourRcFile = "%s/rc.signed" % dirname
  config.router.netid = netid
  config.router.blockBogons = False

  config.network.enableProfiling = False
  config.network.routerProfilesFile = "%s/profiles.dat" % dirname
  config.network.netConfig = {"type": "null"}

  config.netdb.nodedbDir = "%s/netdb" % dirname

  config.system.pidfile = "%s/lokinet.pid" % dirname

  config.dns.netConfig = {"local-dns": ("127.3.2.1:%d" % port)}

  config.bootstrap.routers = ["%s/routers/1/rc.signed" % tmpdir]

  hive.AddRouter(config)

def main(n_routers=10, n_clients=10):
  pyllarp.EnableDebug()
  running = True
  RemoveTmpDir(tmpdir)

  def handle_sigint(sig, frame):
    nonlocal running
    running = False
    print("SIGINT received, attempting to stop all routers")

  signal(SIGINT, handle_sigint)

  hive = pyllarp.RouterHive()
  AddRouter(hive, 1)
  hive.StartAll()
  print("sleeping 2 sec to give plenty of time to save bootstrap rc")
  for i in range(2):
    print(i+1)
    sleep(1)

  print("Resetting hive.  Creating %d routers and %d clients" % (n_routers, n_clients))
  hive.StopAll()

  hive = pyllarp.RouterHive()

  addrs = []

  def onGotEndpoint(ep):
    addr = ep.OurAddress()
    addrs.append(pyllarp.ServiceAddress(addr))

  def sendToAddress(router, toaddr, pkt):
    if router.IsRelay():
      return
    if router.TrySendPacket("default", toaddr, pkt):
      print("sending {} bytes to {}".format(len(pkt), toaddr))

  def broadcastTo(addr, pkt):
    hive.ForEachRouter(lambda r : sendToAddress(r, addr, pkt))

  for i in range(1, n_routers + 1):
    AddRouter(hive, i)

  for i in range(1, n_clients + 1):
    AddClient(hive, i)

  hive.StartAll()
  sleep(1)
  hive.ForEachRouter(lambda r: MakeEndpoint(r, onGotEndpoint))


  while running:
    event = hive.GetNextEvent()
    if event:
      print("Event: %s -- Triggered: %s" % (event.__class__.__name__, event.triggered))
      print(event)
      hops = getattr(event, "hops", None)
      if hops:
        for hop in hops:
          print(hop)

  print('stopping')
  hive.StopAll()
  print('stopped')
  del hive

if __name__ == '__main__':
  main()
