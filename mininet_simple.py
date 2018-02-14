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
    net = Mininet()                                                                                                       
    c0 = net.addController('calvin-controller', controller=RemoteController,
                             ip='10.0.0.250', port=6653)
    
    h1 = net.addHost( 'h1' )                                                                                              
    h2 = net.addHost( 'h2' )                                                                                              
    s1 = net.addSwitch( 's1' )
    net.addLink( h1, s1 )                                                                                                 
    net.addLink( h2, s1 )                                                                                                 
    net.start()
    
    CLI(net)
    net.stop()
