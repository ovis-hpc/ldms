###
file: baler.coffee
author: Narate Taerat (narate at ogc dot us)

This is a javascript package for communicating with bhttpd and constructing
Baler Web GUI widgets.

###

window.D = {} # debugging object.

window.baler =
    UP: "UP"
    DOWN: "DOWN"
    LEFT: "LEFT"
    RIGHT: "RIGHT"

    balerd:
        addr: (()->
            host = window.location.hostname
            if (!host)
                host = 'localhost'
            port = window.location.port
            if (bcfg.bhttpd.host)
                host = bcfg.bhttpd.host
            if (bcfg.bhttpd.port)
                port = bcfg.bhttpd.port
            hostport = host
            if (port)
                hostport += ":#{port}"
            return hostport
        )()

    check_endian: () ->
        # Check browser endianness
        a32 = new Uint32Array(1)
        a8 = new Uint8Array(a32.buffer)
        a32[0] = 1
        if (a8[0] == 1)
            window.baler.little_endian = 1
        else
            window.baler.little_endian = 0

    ntohl: (num) ->
        if not baler.little_endian
            return num
        return ( ((num & 0xFF000000) >> 24) |
                 ((num & 0x00FF0000) >>  8) |
                 ((num & 0x0000FF00) <<  8) |
                 ((num & 0x000000FF) << 24) )

    ###
    left-zero-padding string format for number
    ###
    lzpad: (x, n) ->
        s = ""
        a = x
        while a and n
            a = parseInt(a/10)
            n--
        while n
            s += "0"
            n--
        if x
            s += x
        return s

    ts2datetime: (ts) ->
        date = new Date(ts*1000)
        y = date.getYear() + 1900
        m = baler.lzpad(date.getMonth() + 1, 2)
        d = baler.lzpad(date.getDate(), 2)
        hh = baler.lzpad(date.getHours(), 2)
        mm = baler.lzpad(date.getMinutes(), 2)
        return "#{m}/#{d}/#{y} #{hh}:#{mm}"

    tkn2html : (tok) -> "<span class='baler_#{tok.tok_type}'>#{tok.text}</span>"

    msg2html : (msg) ->
        baler.tkn2html(tok) for tok in msg

    query: (param, cb) ->
        url = "#{bcfg.bhttpd.master_uri}/query"
        $.getJSON(url, param, cb)

    query_img_single: (uri, param, cb) ->
        req = new XMLHttpRequest()
        url = "#{uri}/query"
        first = 1
        for k,v of param
            if (v == undefined)
                continue # skip undefined value
            if first
                c = '?'
                first = 0
            else
                c = '&'
            url += "#{c}#{k}=#{v}"
        req.onload = () -> cb(req.response, "", req)
        req.open("GET", url, true)
        req.responseType = 'arraybuffer'
        req.send()
        return 0

    query_img: (param, cb) ->
        n = 1
        if bcfg.bhttpd.other_uris
            n += bcfg.bhttpd.other_uris.length
        data = null

        local_cb = (resp_data, str, req) ->
            n--
            _data = new Uint32Array(resp_data)
            # aggregate data
            if !data
                data = _data
            else
                for i in [0.._data.length-1]
                    data[i] += _data[i]
            if !n
                cb(data, "", req)

        baler.query_img_single(bcfg.bhttpd.master_uri, param, local_cb)
        if bcfg.bhttpd.other_uris
            for uri in bcfg.bhttpd.other_uris
                baler.query_img_single(uri, param, local_cb)


    get_test: (param, cb) ->
        url = "#{bcfg.bhttpd.master_uri}/test"
        $.getJSON(url, param, cb)

    get_ptns : (cb) ->
        baler.query({"type": "ptn"}, cb)

    get_ptns_simple: (cb) ->
        url = "#{bcfg.bhttpd.master_uri}/query"
        $.get(url, {"type": "ptn_simple"}, (data) ->
            lines = data.split('\n')
            result = []
            for line in lines
                sep_idx = line.search(' ')
                if sep_idx < 0
                    continue # skip invalid line
                ptn_id = line.substr(0, sep_idx)
                ptn_text = line.substr(sep_idx+1, line.length)
                obj = {ptn_id: ptn_id, ptn_text: ptn_text}
                result.push(obj)
            cb_data = {result: result}
            cb(cb_data)
        ) # $.get()

    get_metric_ptns: (cb) ->
        baler.query({"type": "metric_ptn"}, cb)

    get_meta : (cb) ->
        baler.query({"type": "meta"}, cb)

    get_metric_meta : (cb) ->
        baler.query({"type": "metric_meta"}, cb)

    get_big_pic : (done_cb) ->
        url = "#{bcfg.bhttpd.master_uri}/query"
        $.getJSON(url, {"type":"big_pic"}, (data)->
            baler.totalNodes = data.max_comp_id + 1
            baler.min_ts = data.min_ts
            baler.max_ts = data.max_ts
            if (done_cb)
                done_cb(data)
        )

    calcNpp: (npp_p, totalNodes, height) ->
        calc_npp = totalNodes * npp_p
        calc_npp = Math.ceil(calc_npp/height)
        if (!calc_npp)
            calc_npp = 1
        return calc_npp

    msg_cmp: (msg0, msg1) ->
        if msg0.ts == msg1.ts
            if msg0.host == msg1.host
                return 0
            if msg0.host < msg1.host
                return -1
            return 1
        if msg0.ts < msg1.ts
            return -1
        return 1

    meta_cluster: (param, cb) ->
        url = "#{bcfg.bhttpd.master_uri}/meta_cluster"
        $.getJSON(url, param, cb)

    Disp: class Disp
        constructor: (@element_type) ->
            @domobj = LZH.tag(@element_type)

    TokDisp: class TokDisp extends Disp
        constructor: (@tok) ->
            @domobj = LZH.span({class: "baler_#{@tok.tok_type}"}, @tok.text)

    MsgDisp: class MsgDisp extends Disp
        constructor: (@msg) ->
            @domobj = LZH.span(null, t.domobj for t in \
                                (new TokDisp(tok) for tok in @msg))

    MsgLstEnt: class MsgLstEnt extends Disp
        constructor: (@msg) ->
            m = new MsgDisp(@msg.msg)
            ts = LZH.span({class: "timestamp"}, @msg.ts)
            host = LZH.span({class: "host"}, @msg.host)
            @domobj = LZH.li({class: "MsgLstEnt"}, ts, " ", host, " ", m.domobj)

    PtnLstEnt: class PtnLstEnt extends Disp
        constructor: (@ptn) ->
            m = new MsgDisp(@ptn.msg)
            @domobj = LZH.li({class: "PtnLstEnt"}, "[#{@ptn.ptn_id}]:", m.domobj)

    GrpLstEnt: class GrpLstEnt extends Disp
        toggleExpanded: () ->
            @expanded = !@expanded
            if @expanded
                @subdom.style.display = ""
            else
                @subdom.style.display = "none"

        namedom_onClick: (_this_) ->
            _this_.toggleExpanded()

        constructor: (@name, @gid) ->
            @expanded = true
            @namedom = LZH.div({class: "GrpLstEnt_name"}, @name)
            @subdom = LZH.ul()
            @domobj = LZH.li({class: "GrpLstEnt"}, @namedom, @subdom)

            fn = @namedom_onClick
            obj = this
            @namedom.onclick = () -> fn(obj)

        addPtnLstEnt: (p) ->
            @subdom.appendChild(p.domobj)

    # -- end GrpLstEnt class -- #


    PtnTable: class PtnTable extends Disp
        constructor: (__ptns__, @groups, @group_names) ->
            @domobj = LZH.ul({class: "PtnTable"})
            @ptns_ent = [] # ptns_dom[ptn_id] is PtnLstEnt of ptn_id
            @groups_ent = [] # groups_dom[gid] is GrpLstEnt of gid

            @ptns = []
            for p in __ptns__
                @ptns[p.ptn_id] = p
            for pid in Object.keys(@ptns)
                # ptn dom object creation
                ptn = @ptns[pid]
                p = @ptns_ent[pid] = new PtnLstEnt(ptn)
                # group dom object creation
                gid = @groups[pid]
                if not gid
                    gid = 0
                g = @groups_ent[gid]
                if not g
                    gname = "#{gid}.)"
                    if @group_names and @group_names[gid]
                        gname += "  " + @group_names[gid]
                    g = @groups_ent[gid] = new GrpLstEnt(gname, gid)
                    @domobj.appendChild(g.domobj)
                g.addPtnLstEnt(p)

        sort: () ->
            @groups_ent.sort (a,b) ->
                if a.gid == b.gid
                    return 0
                if a.gid < b.gid
                    return -1
                return 1
            for g in @groups_ent
                if g
                    @domobj.appendChild(g.domobj)
            0
    # -- end PtnTable class -- #


    MsgTableControl: class MsgTableControl extends Disp
        getQueryInput: () ->
            q = {}
            for k,obj of @dom.input when obj.value
                q[k] = obj.value
            return q

        on_apply_btn_click: (event) ->
            query = @getQueryInput()
            query.type = "msg"
            query.dir = if query.ts0 then "fwd" else "bwd"
            @msgTable.query(query)

        getOlder: (event, n) ->
            @msgTable.fetchOlder(n)

        constructor: (@msgTable) ->
            @dom =
                root: null
                input:
                    ts0: null
                    ts1: null
                    host_ids: null
                    ptn_ids: null
                apply_btn: null
                up_btn: null
                down_btn: null
                uup_btn: null
                ddown_btn: null

            @dom_input_label_placeholder =
                ts0: ["Timestamp begin: ", "yyyy-mm-dd HH:MM:SS"]
                ts1: ["Timestamp end: ", "yyyy-mm-dd HH:MM:SS"]
                host_ids: ["Host list: ", "example: 1,2,5-10"]
                ptn_ids: ["Pattern ID list: ", "example: 1,2,5-10"]

            _this_ = this
            ul = LZH.ul({style: "list-style: none"})
            for k, [lbl, plc] of @dom_input_label_placeholder
                input = @dom.input[k] = LZH.input()
                input.placeholder = plc
                li = LZH.li(null, LZH.span(null, lbl), input)
                ul.appendChild(li)
            btns = [
                @dom.apply_btn = LZH.button(null, "apply"),
                @dom.uup_btn = LZH.button(null, "\u21c8"),
                @dom.up_btn = LZH.button(null, "\u21bf"),
                @dom.down_btn = LZH.button(null, "\u21c2"),
                @dom.ddown_btn = LZH.button(null, "\u21ca")
            ]

            @dom.apply_btn.onclick = (event) -> _this_.on_apply_btn_click(event)
            @dom.uup_btn.onclick = (event) -> _this_.msgTable.fetchOlder(5)
            @dom.up_btn.onclick = (event) -> _this_.msgTable.fetchOlder(1)
            @dom.down_btn.onclick = (event) -> _this_.msgTable.fetchNewer(1)
            @dom.ddown_btn.onclick = (event) -> _this_.msgTable.fetchNewer(5)

            @dom.root = LZH.div({class: "MsgTableControl"}, ul, btns)
            @domobj = @dom.root

    # -- end MsgTableControl class -- #


    MsgTable: class MsgTable extends Disp
        constructor: (@tableSize = 15) ->
            @query_param =
                type: "msg"
                ts0: undefined
                ts1: undefined
                host_ids: undefined
                ptn_ids: undefined
                session_id: undefined
                n: 10
                dir: "bwd"

            @dom =
                root: null
                ul: null

            @msgent_list = [] # Current list of MsgLstEnt's  .. use msg.ref as key
            @msgent_first = undefined
            @msgent_last = undefined

            @dom.ul = LZH.ul({style: "list-style: none"})
            @domobj = @dom.root = LZH.div(null, @dom.ul)

        clearTable: () ->
            ul = @dom.ul
            ul.removeChild(ul.firstChild) while (ul.firstChild)
            @msgent_list = []

        initTable: (msgs) ->
            @clearTable()
            if @query_param.dir == "fwd"
                @__addFn = @addNewerEnt
            else
                @__addFn = @addOlderEnt
            for msg in msgs
                ent = new MsgLstEnt(msg)
                @__addFn(ent, msg.ref)

        addOlderEnt: (ent, ref) ->
            ent.domobj.msgref = ref
            @msgent_list[ref] = ent
            @dom.ul.insertBefore(ent.domobj, @dom.ul.firstChild)

        addNewerEnt: (ent, ref) ->
            ent.domobj.msgref = ref
            @msgent_list[ref] = ent
            @dom.ul.appendChild(ent.domobj)

        removeMsg: (msgref) ->
            ent = @msgent_list[msgref]
            delete @msgent_list[msgref]
            @dom.ul.removeChild(ent.domobj)

        removeOlderEnt: (n) ->
            if not n
                return
            for i in [1..n]
                @removeMsg(@dom.ul.firstChild.msgref)

        removeNewerEnt: (n) ->
            if not n
                return
            for i in [1..n]
                @removeMsg(@dom.ul.lastChild.msgref)

        __addFn: undefined
        __removeFn: undefined

        updateTable: (msgs) ->
            window.msgs = msgs
            old_refs = Object.keys(@msgent_list)
            if @query_param.dir == "fwd"
                @__addFn = @addNewerEnt
                @__removeFn = @removeOlderEnt
            else
                @__addFn = @addOlderEnt
                @__removeFn = @removeNewerEnt

            ###
            # For debugging #
            for elm in @dom.ul.childNodes
                elm.classList.remove("NewRow")
            # --------------#
            ###
            count = 0
            for msg in msgs
                if @msgent_list[msg.ref]
                    continue
                ent = new MsgLstEnt(msg)
                # ent.domobj.classList.add("NewRow")
                # LZH.addChildren(ent.domobj, " ##{count}")
                count++
                @__addFn(ent, msg.ref)

            @__removeFn(count)

        query_cb: (data, textStatus, jqXHR) ->
            if not data.session_id
                cosole.log("MsgTable query error
                    (check __msgtable_datat for debugging)")
                return -1
            if @query_param.session_id != data.session_id
                # New bhttpd session, treat as new table
                @query_param.session_id = data.session_id
                @initTable(data.msgs)
                return 0
            # Reaching here means updating the table ...
            @updateTable(data.msgs)

        query: (param) ->
            if param
                # Destroy old session_id
                if @query_param.session_id
                    url = "#{bcfg.bhttpd.master_uri}/query/destroy_session?"+
                        "session_id=#{@query_param.session_id}"
                    $.ajax(url, {})
                @query_param = param
            @query_param.n ?= @tableSize
            _this_ = this
            baler.query(@query_param, (data, textStatus, jqXHR) ->
                _this_.query_cb(data, textStatus, jqXHR)
            )

        fetchOlder: (n) ->
            @query_param.n = n
            if @query_param.dir == "fwd"
                @query_param.n += @tableSize - 1
            @query_param.dir = "bwd"
            @query()

        fetchNewer: (n) ->
            this.query_param.n = n
            if @query_param.dir == "bwd"
                @query_param.n += @tableSize - 1
            this.query_param.dir = "fwd"
            this.query()

    # -- end MsgTable class -- #


    HeatMapLayer: class HeatMapLayer extends Disp
        constructor: (@width, @height, @pxlFactor = 10, @ts_begin, @node_begin, @spp, @npp) ->
            _this_ = this
            @min_alpha = 40
            @max_alpha = 200
            @name = "Layer"
            @color = "red"
            @ctxt = undefined
            @pxl = undefined
            @mouseDown = false
            @oldImg = undefined
            @ptn_ids = undefined
            @bound =
                node:
                    min: null
                    max: null
                ts:
                    min: null
                    max: null
            @stores =
                60: "60-1",
                3600: "3600-1"
            @base_color = [255, 0, 0]

            @domobj = LZH.canvas({class: "HeatMapLayer", width: @width, height: @height})
            @domobj.style.position = "absolute"
            @domobj.style.pointerEvents = "none"
            @domobj.style.width = "#{parseInt(@width * @pxlFactor)}px"
            @domobj.style.height = "#{parseInt(@height * @pxlFactor)}px"
            @ctxt = @domobj.getContext("2d")
            @pxl = @ctxt.createImageData(1, 1)
            @ptn_ids = ""

        getStore: () ->
            str = "60-1"
            for k, v of @stores
                if parseInt(@spp) >= parseInt(k)
                    str = v
                else
                    break
            return str

        setHue: (hue) ->
            @base_color = baler.hue2rgb(hue)
            @updateImage()

        updateImageCb: (_data, textStatus, jqXHR, ts0, n0, _w, _h) ->
            img = @ctxt.createImageData(_w, _h)
            i = 0
            data = new Uint32Array(_data)
            while i < data.length
                # data is in network byte order
                d = baler.ntohl(data[i])
                img.data[i*4] = @base_color[0]
                img.data[i*4+1] = @base_color[1]
                img.data[i*4+2] = @base_color[2]
                # img.data[i*4+3] = data[i]
                img.data[i*4+3] = switch
                    when d == 0 then 0
                    when d < @min_alpha then @min_alpha
                    when d > @max_alpha then @max_alpha
                    else d
                i++

            _x = (ts0 - @ts_begin) / @spp
            _y = (n0 - @node_begin) / @npp
            @ctxt.clearRect(_x, _y, _w, _h)
            @ctxt.putImageData(img, _x, _y)

        clearImage: (x = 0, y = 0, width = @width, height = @height) ->
            @ctxt.clearRect(x, y, width, height)

        updateImage: (_x = 0, _y = 0, _width = @width, _height = @height) ->
            _this_ = this
            @clearImage(_x, _y, _width, _height)
            ts0 = @ts_begin + @spp*_x
            ts1 = ts0 + @spp*_width
            n0 = @node_begin + @npp*_y
            n0 = 0 if n0 < 0
            n1 = n0 + @npp*_height
            baler.query_img({
                    type: "img2",
                    ts_begin: ts0,
                    host_begin: n0,
                    #ptn_ids: _this_.ptn_ids.join(","),
                    ptn_ids: @ptn_ids,
                    img_store: _this_.getStore(),
                    width: _width,
                    height: _height,
                    spp: @spp,
                    npp: @npp,
                },
                (data, textStatus, jqXHR)->
                    _this_.updateImageCb(data, textStatus, jqXHR, ts0, n0,
                                                            _width, _height)
            )
            return 0

        nodeSanity: (dly) ->
           nbx = Math.ceil(@node_begin / @npp)
           if nbx<=0 && dly>0
               dly = 0
               return dly
           else if (nbx - dly) < 0
               dly = nbx
               return dly
           else
               return dly

        onMouseMove: (lx, ly) ->
            if ! @mouseDown
                return [0, 0]
            dlx = lx - @mouseDownPos.lx
            dly = ly - @mouseDownPos.ly
            @ctxt.clearRect(0, 0, @width, @height)
            @ctxt.putImageData(@oldImg, dlx, dly)
            return [dlx, dly]

        onMouseDown: (lx, ly) ->
            @mouseDownPos = {lx: lx, ly: ly}
            @oldImg = @ctxt.getImageData(0, 0, @width, @height)
            @mouseDown = true

        onMouseUp: (lx, ly) ->
            [dlx, dly] = @onMouseMove(lx, ly)
            @mouseDown = false
            fx = if dlx < 0 then @width + dlx else 0
            fy = if dly < 0 then @height + dly else 0
            fw = Math.abs(dlx)
            fh = Math.abs(dly)
            ts_begin = @ts_begin - @spp*dlx
            node_begin = @node_begin - @npp*dly
            @ts_begin = ts_begin
            @node_begin = node_begin
            if fw
                @updateImage(fx, 0, fw, @height)
            if fh
                @updateImage(0, fy, @width, fh)

    CanvasLabelV: class CanvasLabelV extends Disp
        constructor: (@width, @height, @inc, @pxlFactor, @labelTextCb, @axis) ->
            @domobj = LZH.canvas({
                            class: "baler_Labels",
                            width: @width,
                            height: @height})
            # canvas pixel = logical pixel * pxlFactor
            # offset is logical
            @offset = 0
            @ctxt = @domobj.getContext("2d")
            @update()

        yy: (start, y) ->
            if @axis == "y"
                return start - y
            return start + y

        getTranslate: () ->
            coff = parseInt(@offset * @pxlFactor)
            if @axis == "y"
                return [0, @height - coff]
            return [0, -coff]

        update: () ->
            @ctxt.setTransform(1, 0, 0, 1, 0, 0)
            @ctxt.clearRect(0, 0, @width, @height)
            [tx, ty] = @getTranslate()
            @ctxt.translate(tx, ty)
            start = parseInt(@offset/@inc) * @inc
            h = parseInt(@height / @inc)
            for y in [0 .. h] by @inc
                lbl = @labelTextCb(Math.abs(@yy(start, y)))
                m = @ctxt.measureText(lbl)
                yy = @yy(start, y)
                x = @width - 20 - m.width
                cyy = yy * @pxlFactor
                @ctxt.fillText(lbl, x, cyy + 10)
                @ctxt.beginPath()
                @ctxt.moveTo(@width - 15, cyy + 5)
                @ctxt.lineTo(@width - 5, cyy + 5)
                stroke = @ctxt.stroke()

        setOffset: (offset) ->
            if @axis == 'y'
                @offset = -offset
            else
                @offset = offset
            @update()

    GridCanvas: class GridCanvas extends Disp
        constructor: (@width=400, @height=400, @pxlFactor=10) ->
            @domobj = LZH.canvas({width: @width+1, height: @height+1})
            @domobj.style.position = "absolute"

            @ctxt = @domobj.getContext("2d")
            @ctxt.beginPath()
            for x in [0..(@width+1)] by @pxlFactor
                @ctxt.moveTo(x+0.5, 0)
                @ctxt.lineTo(x+0.5, @height+1)
                @ctxt.moveTo(0, x+0.5)
                @ctxt.lineTo(@width+1, x+0.5)
            @ctxt.strokeStyle = "#AAA"
            @ctxt.stroke()


    HeatMapDisp: class HeatMapDisp extends Disp
        constructor: (@width=400, @height=400, @spp=3600, @npp=1, @ts_begin=0) ->
            @spp = @spp*1
            @npp = @npp*1
            @ts_begin = parseInt(@ts_begin/ @spp) * @spp
            @node_begin = parseInt(1 / @npp) * @npp
            @layers = undefined
            @pxlFactor = 10
            @limits = {
                min_ts: undefined,
                max_ts: undefined,
                max_comp_id: undefined,
                min_comp_id: undefined,
            }

            @offsetChangeCb = []

            @mouseDown = 0
            @mouseDownPos = {x: 0, y: 0, ts_begin: @ts_begin}

            _this_ = this

            ### Layout construction ###
            textWH = 150

            @gridCanvas = new GridCanvas(@width, @height, @pxlFactor)
            @layerDiv = LZH.div()
            @layerDiv.style.position = "absolute"

            @dispDiv = LZH.div({class: "HeatMapDisp"}, @layerDiv, @gridCanvas.domobj)
            @dispDiv.style.position = "relative"
            @dispDiv.style.width = "#{@width + 1}px"
            @dispDiv.style.height = "#{@height + 1}px"
            @dispDiv.style.transform = "scaleY(-1)"
            @dispDiv.onmousedown = (event) -> _this_.onMouseDown(event)
            @dispDiv.onmouseup = (event) -> _this_.onMouseUp(event)
            @dispDiv.onmousemove = (event) -> _this_.onMouseMove(event)

            lblyfn = (y) ->
                text = "node: #{parseInt((y-1)*_this_.npp)}"
                return text
            lblxfn = (x) ->
                return baler.ts2datetime(x*_this_.spp)

            @xlabel = new CanvasLabelV(textWH, @width, 10, @pxlFactor, lblxfn, 'x')
            @xlabelDiv = @xlabel.domobj
            @xlabelDiv.style.transform = "rotate(-90deg)"
            @xlabelDiv.style.transformOrigin = "0 0 0"
            @xlabelDiv.style.marginTop = "#{textWH}px"
            @xlabel.setOffset(parseInt(@ts_begin / @spp))

            @ylabel = new CanvasLabelV(textWH, @height, 10, @pxlFactor, lblyfn, 'y')
            @ylabelDiv = @ylabel.domobj

            @fillerDiv = LZH.div({class: "HeatMapFillerDiv"})
            @fillerDiv.style.width = "#{textWH}px"
            @fillerDiv.style.height = "#{textWH}px"

            @domobj = LZH.div({"id":"heat_graph"} , @ylabelDiv, @dispDiv, @fillerDiv, @xlabelDiv)

            @layerDescList = []
            @layers = []

        # limits should contain min_ts, max_ts, min_comp_id, max_comp_id
        # undefined parameter means no limit for that parameter
        setLimits: (limits) ->
            @limits = {}
            for k, v of limits
                @limits[k] = v
            0

        createLayer: (name, ptn_ids, base_color = [255, 0, 0]) ->
            width = parseInt(@width / @pxlFactor)
            height = parseInt(@height / @pxlFactor)
            layer = new HeatMapLayer(width, height, @pxlFactor, @ts_begin, @node_begin, @spp, @npp)
            layer.name = name
            layer.base_color = base_color
            layer.ptn_ids = ptn_ids
            layer.ts_begin = @ts_begin
            layer.node_begin = @node_begin
            layer.npp = @npp
            layer.spp = @spp

            layer.updateImage()

            @layers.push(layer)
            @layerDiv.appendChild(layer.domobj)
            # for debugging
            layer.domobj.setAttribute("name", name)
            return @layers.length - 1

        setLayerHue: (idx, hue) ->
            layer = @layers[idx]
            layer.setHue(hue)

        destroyLayer: (idx) ->
            layer = @layers.splice(idx, 1)
            if (!layer)
                return
            @layerDiv.removeChild(layer[0].domobj)

        disableLayer: (idx) ->
            @layers[idx].domobj.hidden = 1

        enableLayer: (idx) ->
            layer = @layers[idx]
            layer.ts_begin = @ts_begin
            layer.node_begin = @node_begin
            layer.npp = @npp
            layer.spp = @spp
            layer.updateImage()
            layer.domobj.hidden = false

        getActiveLayers: () ->
            return (l for l in @layers when !l.domobj.hidden)

        onMouseUp: (event) ->
            if not @mouseDown
                return
            @onMouseMove(event)
            @mouseDown = false
            [lx, ly, dlx, dly] = @getMoveVector(event)
            for l in @layers when !l.domobj.hidden
                l.onMouseUp(lx, ly)
            return 0

        onMouseDown: (event) ->
            if @mouseDown
                return
            @mouseDown = true
            lx = parseInt(event.pageX / @pxlFactor)
            ly = parseInt(event.pageY / @pxlFactor)
            @mouseDownPos.x = event.pageX
            @mouseDownPos.y = event.pageY
            @mouseDownPos.lx = lx
            @mouseDownPos.ly = ly
            @mouseDownPos.ts_begin = @ts_begin
            @mouseDownPos.node_begin = @node_begin
            @mouseDownPos.yoffset = @ylabel.offset
            @mouseDownPos.xoffset = @xlabel.offset
            @mouseDownPos.reflx = parseInt(@ts_begin / @spp)
            @mouseDownPos.refly = parseInt(@node_begin / @npp)
            for l in @layers when !l.domobj.hidden
                l.onMouseDown(lx, ly)
            return 0

        sanity: (min, max, current, diff) ->
            neu = current + diff
            if current < min && diff < 0
                return 0
            if current > max && diff > 0
                return 0
            if neu < min && diff < 0
                neu = min
                diff = min - current
            if neu > max && diff > 0
                neu = max
                diff = max - current
            return diff

        getMoveVector: (event) ->
            lx = parseInt(event.pageX / @pxlFactor)
            ly = parseInt(event.pageY / @pxlFactor)
            dlx = lx - @mouseDownPos.lx
            dly = ly - @mouseDownPos.ly

            # invert y movement
            dly = -dly
            ly = @mouseDownPos.ly + dly

            # Adjust lx, ly, dlx, dly
            min_lts = parseInt(@limits.min_ts / @spp)
            max_lts = parseInt(@limits.max_ts / @spp)
            min_lcid = parseInt(@limits.min_comp_id / @npp)
            max_lcid = parseInt(@limits.max_comp_id / @npp)
            lwidth = parseInt(@width / @pxlFactor) - 1
            lheight = parseInt(@height / @pxlFactor) - 1

            # Adjust actual max to be the max of top-left pixel
            max_lts -= lwidth
            max_lcid -= lheight

            # max should be at least min
            if (max_lts < min_lts)
                max_lts = min_lts
            if (max_lcid < min_lcid)
                max_lcid = min_lcid

            dlx = -@sanity(min_lts, max_lts, @mouseDownPos.reflx, -dlx)
            dly = -@sanity(min_lcid, max_lcid, Math.abs(@mouseDownPos.refly), -dly)
            lx = @mouseDownPos.lx + dlx
            ly = @mouseDownPos.ly + dly

            ret = [lx, ly, dlx, dly]
            return ret

        onMouseMove: (event) ->
            if not @mouseDown
                return 0

            [lx, ly, dlx, dly] = @getMoveVector(event)

            for l in @layers when !l.domobj.hidden
                l.onMouseMove(lx, ly)

            @ts_begin = @mouseDownPos.ts_begin - @spp*dlx
            @node_begin = @mouseDownPos.node_begin - @npp*dly
            xoffset = parseInt(@ts_begin / @spp)
            yoffset = parseInt(@node_begin / @npp)

            @xlabel.setOffset(xoffset)
            @ylabel.setOffset(yoffset)

            @fireOffsetChange()
            return 0

        # Fire offset change event
        fireOffsetChange: () ->
            for cb in @offsetChangeCb
                cb(@ts_begin, @node_begin)

        registerOffsetChangeCb: (cb) ->
            @offsetChangeCb.push(cb)

        updateLayers: (x=0, y=0, width=@width, height=@height) ->
            for l in @layers when !l.domobj.hidden
                l.updateImage(x, y, width/@pxlFactor, height/@pxlFactor)
            return 0

        setNavParam: (@ts_begin, @node_begin, @spp, @npp) ->
            @npp = @npp*1
            @spp = @spp*1
            for l in @layers
                l.ts_begin = @ts_begin
                l.node_begin = @node_begin
                l.spp = @spp
                l.npp = @npp

            xoffset = parseInt(@ts_begin / @spp)
            yoffset = parseInt(@node_begin / @npp)
            @xlabel.setOffset(xoffset)
            @ylabel.setOffset(yoffset)

            @updateLayers()

    HeatMapDispCtrl: class HeatMapDispCtrl extends Disp
        constructor: (@hmap, @totalNodes) ->
            @navCtrl = new HeatMapNavCtrl(@hmap, @totalNodes)
            @layerCtrl = new HeatMapLayerCtrl(@hmap)
            @domobj = LZH.div({class: "HeatMapDispCtrl"}, @navCtrl.domobj, @layerCtrl.domobj)

    HeatMapNavCtrl: class HeatMapNavCtrl extends Disp
        constructor: (@hmap, @totalNodes) ->
            @dom_input_template =
                nav_ts: [
                    "Date/Time: ",
                    "Secons since epoch (e.g. 1428942308)",
                    "baler_nav_ts"
                ]
                nav_node: [
                    "Component ID: ",
                    "component id (e.g. 2)",
                    "baler_nav_node"
                ]
            @dom_select_template =
                spp: [
                    "Image Width: ",
                    "Number of seconds/pixel (default: 2 hours)",
                    "baler_nav_spp"
                ]
                npp: [
                    "Image Height: ",
                    "Number of nodes/pixel (default: 1)",
                    "baler_nav_npp"
                ]
            @wScale =
                spp : {
                    # For the small spp(s), they should be the multiple of 60
                    "Half Hour": 60,
                    "2 Hours": 180,
                    "12 Hours":1080,
                    # For the large spp(s), they should be the multiple of 3600
                    "40 Hours<default>":3600,
                    "3 days":6480,
                    "Week": 14400,
                    "Month": 64800,
                    "Quarter": 194400,
                    "Half Year": 388800,
                    "Year": 788400,
                }
                npp:{
                    "smallest": 0,
                    "25%":.25,
                    "50%":.5,
                    "75%":.75,
                    "100%<default>":1,
                }
            _this_ = this
            ul = LZH.ul({style: "list-style: none"})
            @dom_input = {}
            for k, [lbl, plc, id] of @dom_input_template
                inp = @dom_input[k] = LZH.input({id: id})
                inp.placeholder = plc
                li = LZH.li(null, LZH.span(class: "HeatMapNavCtrlLabel", lbl), inp)
                # Hide the node & date/time navigation control.
                # Uncomment to show it (good for debugging).
                #ul.appendChild(li)
            for i, [lbl, plc, id] of @dom_select_template
                sel = @dom_input[i] = LZH.select({id: id})
                sel.placeholder = plc
                for z, y of @wScale[i]
                    opt = LZH.option()
                    opt.innerHTML = z
                    if (z.match(/<default>/))
                        opt.selected = 1
                    opt.value = y
                    sel.appendChild(opt)
                li = LZH.li(null, LZH.span(class: "HeatMapNavCtrlLabel", lbl), sel)
                ul.appendChild(li)
            ts_text = baler.ts2datetime(@hmap.ts_begin)
            @dom_input.nav_ts.value = ts_text
            @dom_input.nav_node.value = @hmap.node_begin

            # buttons
            @nav_btn = LZH.button({"id":"nav-apply"}, "nav-apply")
            ul.appendChild(LZH.li(null, LZH.span({class: "HeatMapNavCtrlLabel"}), @nav_btn))

            div = LZH.div({"id":"nav-arrows"})
            div0 = LZH.div({"style":"text-align:center"})
            div1 = LZH.div({"style":"text-align:center"})
            div2 = LZH.div({"style":"text-align:center"})

            @nav_up_btn = LZH.button({"id":"nav-up", "class":"nav-dir-btn"}, "")
            $(@nav_up_btn).html("&#9650;")
            div0.appendChild(@nav_up_btn)

            @nav_left_btn = LZH.button({"id":"nav-left", "class":"nav-dir-btn"}, "")
            $(@nav_left_btn).html("&#9668;")
            div1.appendChild(@nav_left_btn)

            @see_all_btn = LZH.button({"id": "see-all", "class":"nav-btn"}, "all")
            div1.appendChild(@see_all_btn)

            @nav_right_btn = LZH.button({"id":"nav-right", "class":"nav-dir-btn"}, "")
            $(@nav_right_btn).html("&#9658;")
            div1.appendChild(@nav_right_btn)

            @nav_down_btn = LZH.button({"id":"nav-down", "class":"nav-dir-btn"}, "&dArr;")
            $(@nav_down_btn).html("&#9660;")
            div2.appendChild(@nav_down_btn)

            LZH.addChildren(div, [div0, div1, div2])
            ul.appendChild(LZH.li(null, LZH.span({class: "HeatMapNavCtrlLabel"}), div))

            @domobj = LZH.div({class: "HeatMapNavCtrl"}, ul)
            @nav_btn.onclick = (e) ->
                _this_.onNavApply()
            @see_all_btn.onclick = (e) ->
                _this_.onSeeAllClicked()
            @nav_up_btn.onclick = (e) ->
                _this_.onNavDirClicked(baler.UP)
            @nav_down_btn.onclick = (e) ->
                _this_.onNavDirClicked(baler.DOWN)
            @nav_left_btn.onclick = (e) ->
                _this_.onNavDirClicked(baler.LEFT)
            @nav_right_btn.onclick = (e) ->
                _this_.onNavDirClicked(baler.RIGHT)

            @hmap.registerOffsetChangeCb((ts, comp) -> _this_.onOffsetChange(ts, comp))

        onNavApply: () ->
            input = @dom_input
            ts = new Date(input.nav_ts.value).getTime()/1000
            comp_id = input.nav_node.value
            spp = input.spp.options[input.spp.selectedIndex].value
            npp_p = input.npp.options[input.npp.selectedIndex].value
            npp = baler.calcNpp(npp_p, @totalNodes, @hmap.height/@hmap.pxlFactor)
            ts = parseInt(ts/spp)*spp
            comp_id = parseInt(comp_id/npp)*npp
            input.nav_node.value = comp_id
            @hmap.setNavParam(ts, comp_id, spp, npp)

        onNavDirClicked: (btnDir) ->
            activeLayers = @hmap.getActiveLayers()
            if (activeLayers.length == 0)
                # do nothing
                return
            ptn_ids = ""
            for layer in activeLayers
                if layer.ptn_ids == undefined
                    ptn_ids = undefined
                    break
                if ptn_ids.length > 0
                    ptn_ids += ","
                ptn_ids += layer.ptn_ids
            params = {
                type: "img_pan",
                ptn_ids: ptn_ids,
                ts_begin: @hmap.ts_begin,
                host_begin: @hmap.node_begin,
                pxl_width: (@hmap.width/@hmap.pxlFactor),
                pxl_height: (@hmap.height/@hmap.pxlFactor),
                spp: @hmap.spp,
                npp: @hmap.npp,
                dir: btnDir,
            }
            _this_ = this
            baler.query params, (ret) ->
                hmap = _this_.hmap
                window.ret = ret # for debugging
                D.oldParam = {
                    ts_begin: hmap.ts_begin,
                    host_begin: hmap.node_begin,
                }
                D.ret = ret
                D.spp = hmap.spp
                D.npp = hmap.npp
                hmap.setNavParam(ret.ts_begin, ret.host_begin, hmap.spp, hmap.npp)

        onSeeAllClicked: () ->
            # come back here
            _this_ = this
            baler.get_big_pic( (data) ->
                min_ts = Math.trunc(data.min_ts / 3600) * 3600
                max_ts = Math.ceil(data.max_ts / 3600) * 3600
                min_comp_id = data.min_comp_id
                max_comp_id = data.max_comp_id
                console.log(_this_.hmap)
                xPixels = _this_.hmap.width / _this_.hmap.pxlFactor
                yPixels = _this_.hmap.height / _this_.hmap.pxlFactor
                nodes = (max_comp_id - min_comp_id + 1)
                npp = Math.ceil(nodes / yPixels)
                spp = Math.ceil((max_ts - min_ts + 1) / xPixels)
                for i, o of _this_.dom_input.spp.options
                    v = o.value
                    if v >= spp
                        spp = v
                        _this_.dom_input.spp.selectedIndex = i
                        console.log("spp index: " + i)
                        break
                for i, o of _this_.dom_input.npp.options
                    v = o.value
                    _npp = baler.calcNpp(v, nodes, yPixels)
                    if _npp >= npp
                        npp = _npp
                        _this_.dom_input.npp.selectedIndex = i
                        console.log("npp index: " + i)
                        break
                # come back here
                ts_text = baler.ts2datetime(min_ts)
                _this_.hmap.setNavParam(min_ts, min_comp_id, spp, npp)
                _this_.onOffsetChange(min_ts, min_comp_id)
            )

        onOffsetChange: (ts_begin, node_begin) ->
            ts_text = baler.ts2datetime(ts_begin)
            @dom_input.nav_ts.value = ts_text
            @dom_input.nav_node.value = node_begin


    HeatMapLayerCtrl: class HeatMapLayerCtrl extends Disp
        constructor: (@hmap) ->
            @dom_input_label_placeholder =
                name: ["Layer name: ", "Any name ...", "layer_name"]
                ptn_ids: ["Pattern ID list: ", "example: 1,2,5-10", "ptn_list"]

            @dom_input = undefined
            @dom_add_btn = undefined
            @dom_layer_list = undefined

            _this_ = this
            @dom_input = {}
            @domobj = LZH.div({class: "HeatMapLayerCtrl"})
            ul = LZH.ul({style: "list-style: none"})
            for k,[lbl,plc, id] of @dom_input_label_placeholder
                inp = @dom_input[k] = LZH.input({id: id})
                inp.placeholder = plc
                li = LZH.li(null, LZH.span(class: "HeatMapLayerCtrlLabel", lbl), inp)
                ul.appendChild(li)
            @dom_add_btn = LZH.button({"id":"add_map"}, "add")
            @dom_add_btn.onclick = () -> _this_.onAddBtnClick()
            ul.appendChild(LZH.li(null, LZH.span({class: "HeatMapLayerCtrlLabel"}), @dom_add_btn))

            @dom_layer_list = LZH.ul({style: "list-style: none"})

            # Laying out the component
            @domobj.appendChild(ul)
            @domobj.appendChild(@dom_layer_list)
            return this

        onAddBtnClick: () ->
            _this_ = this
            name = @dom_input["name"].value
            ptn_ids = @dom_input["ptn_ids"].value
            if (! ptn_ids)
                ptn_ids = undefined
            hue = 0
            idx = @hmap.createLayer(name, ptn_ids, baler.hue2rgb(hue))
            @dom_input["name"].value = ""
            @dom_input["ptn_ids"].value = ""

            chk = LZH.input({type: "checkbox"})
            chk.checked = 1
            chk.layer = @hmap.layers[idx]
            chk.onchange = () -> _this_.onLayerCheckChange(chk)

            rmbtn = LZH.button({class: "remove_ptn"}, "x")
            rmbtn.layer = @hmap.layers[idx]
            rmbtn.onclick = () -> _this_.onRmBtnClicked(rmbtn)

            cpick = new ColorPicker(hue, (hue) -> _this_.onPickerHueChange(hue, idx))
            ptn_ids_text = if (ptn_ids) then (ptn_ids) else ("Everything")

            li = LZH.li({"style":"width:250px;overflow:auto;"},
                            chk, cpick.domobj,
                            name, ":", ptn_ids_text, " ", rmbtn)

            rmbtn.li = li
            @dom_layer_list.appendChild(li)

        onPickerHueChange: (hue, idx) ->
            @hmap.setLayerHue(idx, hue)

        onLayerCheckChange: (chk) ->
            idx = @hmap.layers.indexOf(chk.layer)
            if chk.checked
                @hmap.enableLayer(idx)
            else
                @hmap.disableLayer(idx)

        onRmBtnClicked: (btn) ->
            idx = @hmap.layers.indexOf(btn.layer)
            @dom_layer_list.removeChild(btn.li)
            @hmap.destroyLayer(idx)

    hue2rgb: (hue) ->
        hue %= 360
        h = hue/60
        c = 255
        x = (1 - Math.abs(h%2-1))*255
        i = Math.floor(h)
        return switch (i)
            when 0
                return [c, x, 0]
            when 1
                return [x, c, 0]
            when 2
                return [0, c, x]
            when 3
                return [0, x, c]
            when 4
                return [x, 0, c]
            when 5
                return [c, 0, x]

    hue2hex: (hue) ->
        return baler.rgb2hex(baler.hue2rgb(hue))

    rgb2hex: (rgb) ->
        s = (("0#{parseInt(x).toString(16)}").slice(-2) for x in rgb)
        str = "##{s[0]}#{s[1]}#{s[2]}"
        return str

    ColorPicker: class ColorPicker extends Disp
        constructor: (@hue, @colorSetCb) ->
            _this_ = this
            @box = LZH.span({class: "ColorBox"}, " ")
            @input = LZH.input({type: "range", value: "0", min: "0", max: "100"})
            @domobj = LZH.span({class: "ColorPicker"}, @box, LZH.span({}, @input))
            @setBoxColor(@hue)
            @input.oninput = () -> _this_.oninput()
            @input.onchange = () -> _this_.onchange()

        oninput: () ->
            v = @input.value
            hue = parseInt(360 * (v / 96))
            @setBoxColor(hue)

        onchange: () ->
            window.ccb = @colorSetCb
            if not @colorSetCb
                return
            v = @input.value
            hue = parseInt(360 * (v / 96))
            @colorSetCb(hue)

        setBoxColor: (hue) ->
            @hue = hue
            color = baler.hue2hex(hue)
            @box.style.backgroundColor = color

    MetaClusterStat: class MetaClusterStat extends Disp
        # state <--> text mapping is static
        stateMap: {
            BMPTN_STORE_STATE_NA: "N/A State",
            BMPTN_STORE_STATE_ERROR: "ERROR!",
            BMPTN_STORE_STATE_INITIALIZED: "Initialized",
            BMPTN_STORE_STATE_META_1: "Clustering lv 1",
            BMPTN_STORE_STATE_META_2: "Clustering lv 2",
            BMPTN_STORE_STATE_REFINING: "Refining",
            BMPTN_STORE_STATE_NAMING: "Naming",
            BMPTN_STORE_STATE_DONE: "Complete"
        }

        constructor: () ->
            @stat = LZH.span({class:"MetaClusterStatLabel"}, "")
            @progress = LZH.progress({max: "100"})
            @domobj = LZH.div({class: "MetaClusterStat"}, @stat, @progress)
            @doneCb = null

        updateTilDone: () ->
            _this_ = this
            @domobj.hidden = false
            baler.meta_cluster(null, (data, status, jqXHR) -> _this_.updateTilDoneCb(data, status, jqXHR))

        updateTilDoneCb: (data, status, jqXHR) ->
            _this_ = this
            @stat.innerText = @stateMap[data.state]
            @progress.value = data.percent
            switch data.state
                when "BMPTN_STORE_STATE_INITIALIZED", \
                    "BMPTN_STORE_STATE_META_1", \
                    "BMPTN_STORE_STATE_META_2", \
                    "BMPTN_STORE_STATE_REFINING", \
                    "BMPTN_STORE_STATE_NAMING"
                        window.setTimeout((() -> _this_.updateTilDone()), 500)
                else
                    if @doneCb
                        @doneCb(data)

        update: () ->
            _this_ = this
            baler.meta_cluster(null, (data, status, jqXHR)-> _this_.updateCb(data, status, jqXHR))

        updateCb: (data, status, jqXHR) ->
            @stat.innerText = @stateMap[data.state]
            @progress.value = data.percent

    MetaClusterCtrl: class MetaPtnCtrl extends Disp
        constructor: () ->
            _this_ = this
            @domobj = LZH.div({class: "MetaClusterCtrl"})

            # Parameter control
            @param_lbl = {
                # [label, placeholder]
                refinement_speed: ["Refinement Speed", "value > 1.0, e.g. 2.0", "baler_refinement_speed"],
                looseness: ["Looseness", "value in (0.0 - 1.0)", "baler_looseness"],
                diff_ratio: ["Difference Ratio", "value in (0.0 - 1.0)", "baler_diff_ratio"]
            }
            @param_input = {}
            ul = LZH.ul({style: "list-style: none"})

            # Clustering Status
            @stat = new MetaClusterStat()
            @stat.domobj.hidden = 1
            @stat.doneCb = (data) -> _this_.onStatDone(data)
            li = LZH.li(null, @stat.domobj)
            ul.appendChild(li)

            # Paremeter input
            for k, [lbl, plc, id] of @param_lbl
                inp = @param_input[k] = LZH.input({id: id})
                inp.placeholder = plc
                inp.onblur = () -> @value = @value.replace(/^\s+|\s+$/g, '')
                li = LZH.li(null, LZH.span(class: "MetaClusterCtrlLabel", lbl), inp)
                ul.appendChild(li)

            # button setup
            @btn = LZH.button(null, "run meta cluster")
            ul.appendChild(LZH.li(null, LZH.span({class: "MetaClusterCtrlLabel"}), @btn))
            @btn.onclick = () -> _this_.onBtnClick()

            @domobj.appendChild(ul)
            @doneCb = null

        setDoneCb: (fn) ->
            @doneCb = fn

        onBtnClick: () ->
            param = {op: "run"}
            for k, inp of @param_input
                str = inp.value
                param[k] = str if str
            baler.meta_cluster(param)
            @stat.updateTilDone()

        onStatDone: (data) ->
            _this_ = this
            window.setTimeout((()-> _this_.stat.domobj.hidden = 1), 1000)
            @doneCb(data) if @doneCb

    Parent: class Parent
        constructor: (@name) ->
            console.log("parent constructor")

        say: (text) ->
            console.log("#{@name}: #{text}")

    Child: class Child extends Parent
        constructor: (@name) ->
            super(name)
            console.log("child constructor")

        say: (text) ->
            console.log("child .. #{@name}: #{text}")

window.baler.check_endian()

# END OF FILE #
