/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

var { ICAL, unwrapSetter, wrapGetter } = ChromeUtils.import("resource://calendar/modules/ical.js");

function calPeriod(innerObject) {
    this.innerObject = innerObject || new ICAL.Period({});
    this.wrappedJSObject = this;
}

calPeriod.prototype = {
    QueryInterface: ChromeUtils.generateQI([Ci.calIPeriod]),
    classID: Components.ID("{394a281f-7299-45f7-8b1f-cce21258972f}"),

    isMutable: true,
    innerObject: null,

    get icalPeriod() { return this.innerObject; },
    set icalPeriod(val) { this.innerObject = val; },

    makeImmutable: function() { this.isMutable = false; },
    clone: function() { return new calPeriod(this.innerObject.clone()); },

    get start() { return wrapGetter(calDateTime, this.innerObject.start); },
    set start(rawval) {
        unwrapSetter(ICAL.Time, rawval, function(val) {
            this.innerObject.start = val;
        }, this);
    },

    get end() { return wrapGetter(calDateTime, this.innerObject.getEnd()); },
    set end(rawval) {
        unwrapSetter(ICAL.Time, rawval, function(val) {
            if (this.innerObject.duration) {
                this.innerObject.duration = null;
            }
            this.innerObject.end = val;
        }, this);
    },

    get duration() { return wrapGetter(calDuration, this.innerObject.getDuration()); },

    get icalString() { return this.innerObject.toICALString(); },
    set icalString(val) {
        let dates = ICAL.parse._parseValue(val, "period", ICAL.design.icalendar);
        this.innerObject = ICAL.Period.fromString(dates.join("/"));
        return val;
    },

    toString: function() { return this.innerObject.toString(); }
};
