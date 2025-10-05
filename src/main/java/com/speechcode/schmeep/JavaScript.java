package com.speechcode.schmeep;

public class JavaScript {
    public static String escape(String input) {
	return input.replace("\\", "\\\\")
	    .replace("\"", "\\\"")
	    .replace("\n", "\\n")
	    .replace("\r", "\\r")
	    .replace("\t", "\\t");
    }
}
