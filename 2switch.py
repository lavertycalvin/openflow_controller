
from mininet.topo import Topo
from mininet.net import Mininet
from mininet.node import OVSSwitch, Controller, RemoteController

class RectTopo(Topo):
    def __init__(self):
            Topo.__init__(self)

            h1 = self.addHost('h1')
            h2 = self.addHost('h2')
            h3 = self.addHost('h3')
            h4 = self.addHost('h4')
            s1 = self.addSwitch('s1')
            s2 = self.addSwitch('s2')

            #links
            self.addLink(h1,s1)
            self.addLink(h2,s1)
            self.addLink(h3,s2)
            self.addLink(h4,s2)
            self.addLink(s1,s2)


topos = { 'recttopo': (lambda: RectTopo())}
