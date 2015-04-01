t = $("#test")[0]
baler.get_ptns (data, textStatus, jqXHR) ->
    window.patterns = data.result
    baler.get_meta (data, textStatus, jqXHR) ->
        window.map = data.map
        grp = []
        for p in data.map
            grp[p[0]] = p[1]
        window.grp = grp
        p = new baler.PtnTable(window.patterns, grp, data.cluster_names)
        p.sort()
        t.appendChild(p.domobj)
        window.p = p
0
