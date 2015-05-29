t = $("#test")[0]
t.style.position = "relative"

baler.get_big_pic( (data) ->
    hmap = window.hmap = new baler.HeatMapDisp(400,400,3600*2,2)
    hmap.setLimits(data)
    window.hmapCtrl = new baler.HeatMapDispCtrl(hmap, data.max_comp_id + 1)
    hmapCtrl.domobj.style.float = "left"
    hmap.domobj.style.float = "left"

    hmapCtrl.navCtrl.dom_input.nav_ts.value = baler.ts2datetime(data.min_ts)
    hmapCtrl.navCtrl.onNavApply()

    t.appendChild(hmap.domobj)
    t.appendChild(hmapCtrl.domobj)

    hmap.updateLayers()
)

0
