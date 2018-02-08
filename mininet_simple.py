#!/usr/bin/python2
"""This script spins up a basic instance of Mininet with one switch, one
controller and two hosts"""


from mininet.net  import Mininet
from mininet.node import RemoteController
from mininet.topo import SingleSwitchTopo
from mininet.log  import setLogLevel
from mininet.cli  import CLI

if __name__ == '__main__':
    setLogLevel('info')
    NET_SIMPLE = Mininet(topo=SingleSwitchTopo(2), controller=None)
    NET_SIMPLE.addController('calvin-controller', controller=RemoteController,
                             ip='127.0.0.1', port=6653)
    NET_SIMPLE.start()
    CLI(NET_SIMPLE)
    NET_SIMPLE.stop()
