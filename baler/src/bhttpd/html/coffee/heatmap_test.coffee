t = $("#test")[0]
t.style.position = "relative"

hmap = window.hmap = new baler.HeatMapDisp(400,400,3600*2,2)

###
hmap.createLayer("L1", "128", [255, 0, 0])
hmap.createLayer("L2", "129", [0, 0, 255])
###

hmapCtrl = new baler.HeatMapDispCtrl(hmap)

###
hmap = window.hmap = new baler.HeatMapDisp(400, 400)
window.hmap.bound.node.min = 0
window.hmap.bound.node.max = 700
window.hmap.bound.ts.min = 1425963600
window.hmap.bound.ts.max = 1425963600 + 3600*700
window.hmap.ptn_ids.push(128)

hmap.domobj.style.position = "absolute"
hmap.domobj.style.left = 0
hmap.domobj.style.top = 0

hmap2 = window.hmap2 = new baler.HeatMapDisp(400, 400)
hmap2.bound.node.min = 0
hmap2.bound.node.max = 700
hmap2.bound.ts.min = 1425963600
hmap2.bound.ts.max = 1425963600 + 3600*700
hmap2.ptn_ids.push(129)

hmap2.domobj.style.position = "absolute"
hmap2.domobj.style.left = 0
hmap2.domobj.style.top = 0

# window.hmap.updateImage(0, 0, 100, 100)
window.hmap.updateImage()
window.hmap2.updateImage()

###

hmapCtrl.domobj.style.float = "left"
hmap.domobj.style.float = "left"

t.appendChild(hmap.domobj)
t.appendChild(hmapCtrl.domobj)

hmap.updateLayers()

0
