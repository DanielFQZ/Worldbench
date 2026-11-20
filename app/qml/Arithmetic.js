.pragma library
// Bounded arithmetic parser. Deliberately accepts no names, calls or JavaScript code.
function calculate(text) {
    if(text.length>128) return NaN
    var p=0,depth=0
    function space(){while(p<text.length && /\s/.test(text[p]))p++}
    function atom(){
        space();if(++depth>24)throw 0
        var sign=1
        if(text[p]==='+'||text[p]==='-'){if(text[p++]==='-')sign=-1;space()}
        var n
        if(text[p]==='('){p++;n=sum();space();if(text[p++]!==')')throw 0}
        else {var m=/^(?:\d+(?:\.\d*)?|\.\d+)/.exec(text.slice(p));if(!m)throw 0;p+=m[0].length;n=Number(m[0])}
        depth--;return sign*n
    }
    function product(){var a=atom();space();while(text[p]==='*'||text[p]==='/'){var op=text[p++],b=atom();if(op==='/'&&b===0)throw 0;a=op==='*'?a*b:a/b;space()}return a}
    function sum(){var a=product();space();while(text[p]==='+'||text[p]==='-'){var op=text[p++],b=product();a=op==='+'?a+b:a-b;space()}return a}
    try{var n=sum();space();return p===text.length&&isFinite(n)&&Math.abs(n)<=1e12?n:NaN}catch(e){return NaN}
}
