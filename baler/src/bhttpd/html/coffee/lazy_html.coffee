define () ->
    lazy_html =
        addChildren: (e, children = []) ->
            for x in children
                if Object.prototype.toString.call(x) == "[object Array]"
                    lazy_html.addChildren(e, x)
                else if typeof x == "object"
                    e.appendChild(x)
                else
                    e.appendChild(document.createTextNode(x))

        tag: (tag, attr = [], children = []) ->
            e = document.createElement(tag)
            for k, v of attr
                e.setAttribute(k, v)
            @addChildren(e, children)
            return e

    tags = [ "span", "li", "ul", "input", "div", "button", "canvas"]

    for tag in tags
        do (tag) ->
            lazy_html[tag] = (attr, children...) ->
                lazy_html.tag(tag, attr, children)

    return lazy_html
